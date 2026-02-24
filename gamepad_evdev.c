// x360_uinput_bridge.c
// Build: gcc -O2 -Wall x360_uinput_bridge.c -lusb-1.0 -o x360_uinput_bridge
//
// Run:
//   sudo modprobe uinput
//   sudo ./x360_uinput_bridge

#include <libusb-1.0/libusb.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>

static const uint16_t VID = 0x045E;
static const uint16_t PID = 0x028F;

static int16_t s16le(const uint8_t *b, int off) {
    return (int16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1] << 8));
}

static uint32_t u32le(const uint8_t *b, int off) {
    return (uint32_t)b[off]
        | ((uint32_t)b[off + 1] << 8)
        | ((uint32_t)b[off + 2] << 16)
        | ((uint32_t)b[off + 3] << 24);
}

static int emit(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) return -1;
    return 0;
}

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void set_abs(int fd, int code, int minv, int maxv, int fuzz, int flat) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof(a));
    a.code = code;
    a.absinfo.minimum = minv;
    a.absinfo.maximum = maxv;
    a.absinfo.fuzz = fuzz;
    a.absinfo.flat = flat;
    if (ioctl(fd, UI_ABS_SETUP, &a) < 0) die("UI_ABS_SETUP");
}

static int setup_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) die("open /dev/uinput");

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);

    // Face buttons
    ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH); // A
    ioctl(fd, UI_SET_KEYBIT, BTN_EAST);  // B
    ioctl(fd, UI_SET_KEYBIT, BTN_WEST);  // X
    ioctl(fd, UI_SET_KEYBIT, BTN_NORTH); // Y

    // Shoulders
    ioctl(fd, UI_SET_KEYBIT, BTN_TL); // LB
    ioctl(fd, UI_SET_KEYBIT, BTN_TR); // RB

    // Start/Back
    ioctl(fd, UI_SET_KEYBIT, BTN_START);
    ioctl(fd, UI_SET_KEYBIT, BTN_SELECT);

    // NEW BUTTONS
    ioctl(fd, UI_SET_KEYBIT, BTN_MODE);    // Guide
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBL);  // L3
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBR);  // R3

    // Axes
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);
    ioctl(fd, UI_SET_ABSBIT, ABS_RY);
    ioctl(fd, UI_SET_ABSBIT, ABS_Z);
    ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);

    set_abs(fd, ABS_X,  -32768, 32767, 16, 128);
    set_abs(fd, ABS_Y,  -32768, 32767, 16, 128);
    set_abs(fd, ABS_RX, -32768, 32767, 16, 128);
    set_abs(fd, ABS_RY, -32768, 32767, 16, 128);

    set_abs(fd, ABS_Z,   0, 255, 0, 0);
    set_abs(fd, ABS_RZ,  0, 255, 0, 0);

    set_abs(fd, ABS_HAT0X, -1, 1, 0, 0);
    set_abs(fd, ABS_HAT0Y, -1, 1, 0, 0);

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    snprintf(us.name, UINPUT_MAX_NAME_SIZE, "x360 vendor bridge (evdev)");
    us.id.bustype = BUS_USB;
    us.id.vendor  = VID;
    us.id.product = PID;
    us.id.version = 1;

    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);

    usleep(200000);
    return fd;
}

static int open_usb(libusb_context **ctx, libusb_device_handle **h) {
    libusb_init(ctx);
    *h = libusb_open_device_with_vid_pid(*ctx, VID, PID);
    if (!*h) return -1;

    int ifc = 0;
    if (libusb_kernel_driver_active(*h, ifc) == 1)
        libusb_detach_kernel_driver(*h, ifc);

    libusb_set_configuration(*h, 1);
    libusb_claim_interface(*h, ifc);

    return libusb_control_transfer(*h, 0x40, 0x48, 0x0006, 0x0000, NULL, 0, 1000);
}

static void hat_from_dpad(uint32_t btn, int *hatx, int *haty) {
    int x = 0, y = 0;
    if (btn & 0x00000004u) x -= 1;
    if (btn & 0x00000008u) x += 1;
    if (btn & 0x00000001u) y -= 1;
    if (btn & 0x00000002u) y += 1;
    *hatx = x;
    *haty = y;
}

int main(void) {
    int ufd = setup_uinput();

    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;
    if (open_usb(&ctx, &h) < 0) {
        fprintf(stderr, "USB open failed\n");
        return 1;
    }

    uint32_t p_btn = 0;
    int16_t  p_lx = 0, p_ly = 0, p_rx = 0, p_ry = 0;
    uint8_t  p_lt = 0, p_rt = 0;
    int      p_hatx = 0, p_haty = 0;

    while (1) {
        uint8_t report[20];
        memset(report, 0, sizeof(report));

        int r = libusb_control_transfer(
            h, 0xC0, 0xC2, 0, 0,
            report, sizeof(report), 1000
        );

        if (r < 14) continue;

        uint32_t btn = u32le(report, 2);
        uint8_t lt = report[4];
        uint8_t rt = report[5];

        int16_t lx = s16le(report, 6);
        int16_t ly = s16le(report, 8);
        int16_t rx = s16le(report, 10);
        int16_t ry = s16le(report, 12);

        int hatx, haty;
        hat_from_dpad(btn, &hatx, &haty);

        int a = (btn & 0x00001000u) ? 1 : 0;
        int b = (btn & 0x00002000u) ? 1 : 0;
        int x = (btn & 0x00004000u) ? 1 : 0;
        int y = (btn & 0x00008000u) ? 1 : 0;
        int lb = (btn & 0x00000100u) ? 1 : 0;
        int rb = (btn & 0x00000200u) ? 1 : 0;
        int start = (btn & 0x00000010u) ? 1 : 0;
        int back  = (btn & 0x00000020u) ? 1 : 0;

        int guide = (btn & 0x00000400u) ? 1 : 0;
        int l3    = (btn & 0x00000040u) ? 1 : 0;
        int r3    = (btn & 0x00000080u) ? 1 : 0;

        int changed = 0;
        if (btn != p_btn || lx != p_lx || ly != p_ly ||
            rx != p_rx || ry != p_ry ||
            lt != p_lt || rt != p_rt ||
            hatx != p_hatx || haty != p_haty)
            changed = 1;

        if (changed) {
            emit(ufd, EV_KEY, BTN_SOUTH, a);
            emit(ufd, EV_KEY, BTN_EAST,  b);
            emit(ufd, EV_KEY, BTN_WEST,  y);
            emit(ufd, EV_KEY, BTN_NORTH, x);
            emit(ufd, EV_KEY, BTN_TL, lb);
            emit(ufd, EV_KEY, BTN_TR, rb);
            emit(ufd, EV_KEY, BTN_START, start);
            emit(ufd, EV_KEY, BTN_SELECT, back);

            emit(ufd, EV_KEY, BTN_MODE, guide);
            emit(ufd, EV_KEY, BTN_THUMBL, l3);
            emit(ufd, EV_KEY, BTN_THUMBR, r3);

            emit(ufd, EV_ABS, ABS_HAT0X, hatx);
            emit(ufd, EV_ABS, ABS_HAT0Y, haty);

            emit(ufd, EV_ABS, ABS_X,  lx);
            emit(ufd, EV_ABS, ABS_Y, -ly);
            emit(ufd, EV_ABS, ABS_RX, rx);
            emit(ufd, EV_ABS, ABS_RY, -ry);

            emit(ufd, EV_ABS, ABS_Z,  lt);
            emit(ufd, EV_ABS, ABS_RZ, rt);

            emit(ufd, EV_SYN, SYN_REPORT, 0);

            p_btn = btn;
            p_lx = lx; p_ly = ly; p_rx = rx; p_ry = ry;
            p_lt = lt; p_rt = rt;
            p_hatx = hatx; p_haty = haty;
        }

        usleep(2000);
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}