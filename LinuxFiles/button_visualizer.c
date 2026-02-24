// x360_buttons_live.c
// Build: gcc -O2 -Wall x360_buttons_live.c -lusb-1.0 -o x360_buttons_live

#include <libusb-1.0/libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const uint16_t VID = 0x045E;
static const uint16_t PID = 0x028F;

typedef struct {
    uint32_t mask;
    const char *name;
} Btn;

static const Btn BUTTONS[] = {
    {0x00001000u, "A"},
    {0x00002000u, "B"},
    {0x00004000u, "X"},
    {0x00008000u, "Y"},

    {0x00000001u, "DPAD_UP"},
    {0x00000002u, "DPAD_DOWN"},
    {0x00000004u, "DPAD_LEFT"},
    {0x00000008u, "DPAD_RIGHT"},

    {0x00000010u, "START"},
    {0x00000020u, "BACK"},

    {0x00000100u, "LB"},
    {0x00000200u, "RB"},
};

static uint32_t u32le(const uint8_t *b, int off) {
    return (uint32_t)b[off]
        | ((uint32_t)b[off + 1] << 8)
        | ((uint32_t)b[off + 2] << 16)
        | ((uint32_t)b[off + 3] << 24);
}

static void print_held(uint32_t btn) {
    int any = 0;
    for (size_t i = 0; i < sizeof(BUTTONS)/sizeof(BUTTONS[0]); i++) {
        if (btn & BUTTONS[i].mask) {
            if (any) printf(" + ");
            printf("%s", BUTTONS[i].name);
            any = 1;
        }
    }
    if (!any) printf("(none)");
}

int main(void) {

    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    if (libusb_init(&ctx) != 0) return 1;

    h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) {
        fprintf(stderr, "Device not found\n");
        return 1;
    }

    int ifc = 0;

    if (libusb_kernel_driver_active(h, ifc) == 1)
        libusb_detach_kernel_driver(h, ifc);

    libusb_set_configuration(h, 1);
    libusb_claim_interface(h, ifc);

    // Enable stream
    libusb_control_transfer(h, 0x40, 0x48, 0x0006, 0x0000, NULL, 0, 1000);

    printf("Live Xbox 360 Parser (buttons + triggers)\n\n");

    uint32_t prev_btn = 0xFFFFFFFFu;
    uint8_t  prev_lt  = 0xFF;
    uint8_t  prev_rt  = 0xFF;

    while (1) {

        uint8_t report[20] = {0};

        int r = libusb_control_transfer(
            h,
            0xC0,
            0xC2,
            0x0000,
            0x0000,
            report,
            sizeof(report),
            1000
        );

        if (r < 0) break;
        if (r < 6) { usleep(2000); continue; }

        uint32_t btn = u32le(report, 2);

        uint8_t lt = report[4];  // Left trigger
        uint8_t rt = report[5];  // Right trigger

        int changed = 0;

        if (btn != prev_btn) changed = 1;
        if (lt != prev_lt) changed = 1;
        if (rt != prev_rt) changed = 1;

        if (changed) {

            float lt_pct = (lt / 255.0f) * 100.0f;
            float rt_pct = (rt / 255.0f) * 100.0f;

            printf("btn=0x%08X  | held: ", btn);
            print_held(btn);

            printf("  | LT=%3u (%5.1f%%)", lt, lt_pct);
            printf("  | RT=%3u (%5.1f%%)", rt, rt_pct);
            printf("\n");

            fflush(stdout);

            prev_btn = btn;
            prev_lt  = lt;
            prev_rt  = rt;
        }

        usleep(2000);
    }

    libusb_release_interface(h, ifc);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}