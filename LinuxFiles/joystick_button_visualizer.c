// x360_live.c
// Build: gcc -O2 -Wall x360_live.c -lusb-1.0 -o x360_live
//
// Live terminal UI for your vendor control-transfer poll stream.
// Shows:
//  - Left/Right stick ASCII circles (same as your original)
//  - Buttons held (A/B/X/Y, D-pad, Start/Back, LB/RB)
//  - Triggers LT/RT as raw + percentage (LT=byte4, RT=byte5)
//
// Decoding (from your packets):
//   btn_field: bytes 2–5 (u32 LE), but note bytes[4]=LT and bytes[5]=RT inside that dword
//   Left stick:  bytes 6–9   -> LX (6–7 LE s16), LY (8–9 LE s16)
//   Right stick: bytes 10–13 -> RX (10–11 LE s16), RY (12–13 LE s16)
//   LT: byte 4 (0x00..0xFF)  RT: byte 5 (0x00..0xFF)
//
// Quit: Ctrl+C

#include <libusb-1.0/libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

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

static int16_t s16le(const uint8_t *b, int off) {
    return (int16_t)((uint16_t)b[off] | ((uint16_t)b[off + 1] << 8));
}

static uint32_t u32le(const uint8_t *b, int off) {
    return (uint32_t)b[off]
        | ((uint32_t)b[off + 1] << 8)
        | ((uint32_t)b[off + 2] << 16)
        | ((uint32_t)b[off + 3] << 24);
}

static float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void draw_stick_grid(char *out, int out_stride, int size, float xn, float yn,
                            const char *title, int16_t xraw, int16_t yraw) {
    const float cx = (size - 1) / 2.0f;
    const float cy = (size - 1) / 2.0f;
    const float r  = (size - 1) / 2.0f;

    int px = (int)lroundf(cx + xn * r);
    int py = (int)lroundf(cy - yn * r); // invert Y so up is positive
    if (px < 0) px = 0; if (px >= size) px = size - 1;
    if (py < 0) py = 0; if (py >= size) py = size - 1;

    snprintf(out + 0 * out_stride, out_stride, "%-12s  X=%6d  Y=%6d", title, (int)xraw, (int)yraw);

    for (int y = 0; y < size; y++) {
        char *row = out + (1 + y) * out_stride;
        for (int x = 0; x < size; x++) row[x] = ' ';
        row[size] = '\0';

        for (int x = 0; x < size; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx*dx + dy*dy);

            if (fabsf(dist - r) < 0.55f) row[x] = '.';

            if (x == (int)lroundf(cx)) row[x] = '|';
            if (y == (int)lroundf(cy)) row[x] = '-';
            if (x == (int)lroundf(cx) && y == (int)lroundf(cy)) row[x] = '+';
        }

        if (y == py) row[px] = 'O';
    }
}

static void format_held_buttons(char *out, size_t out_sz, uint32_t btn) {
    size_t n = 0;
    int any = 0;
    for (size_t i = 0; i < sizeof(BUTTONS)/sizeof(BUTTONS[0]); i++) {
        if (btn & BUTTONS[i].mask) {
            const char *name = BUTTONS[i].name;
            int wrote = snprintf(out + n, (n < out_sz) ? (out_sz - n) : 0,
                                 "%s%s", any ? " + " : "", name);
            if (wrote > 0) n += (size_t)wrote;
            any = 1;
        }
    }
    if (!any) snprintf(out, out_sz, "(none)");
}

int main(void) {
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    int r = libusb_init(&ctx);
    if (r != 0) {
        fprintf(stderr, "libusb_init failed: %d\n", r);
        return 1;
    }

    h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) {
        fprintf(stderr, "Device not found (VID=%04x PID=%04x)\n", VID, PID);
        libusb_exit(ctx);
        return 1;
    }

    int ifc = 0;

    if (libusb_kernel_driver_active(h, ifc) == 1) {
        (void)libusb_detach_kernel_driver(h, ifc);
    }

    (void)libusb_set_configuration(h, 1);

    r = libusb_claim_interface(h, ifc);
    if (r != 0) {
        fprintf(stderr, "claim_interface(%d) failed: %d\n", ifc, r);
        fprintf(stderr, "Try sudo or a udev rule, or change interface number.\n");
        libusb_close(h);
        libusb_exit(ctx);
        return 1;
    }

    // Enable/arm: vendor OUT, no data stage
    r = libusb_control_transfer(
        h,
        0x40,
        0x48,
        0x0006,
        0x0000,
        NULL, 0,
        1000
    );
    if (r < 0) {
        fprintf(stderr, "enable control_transfer failed: %d\n", r);
        libusb_release_interface(h, ifc);
        libusb_close(h);
        libusb_exit(ctx);
        return 1;
    }

    // Terminal: clear screen, hide cursor
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);

    const int GRID = 21;
    const int STRIDE = 128;
    char left_buf[(1 + GRID) * STRIDE];
    char right_buf[(1 + GRID) * STRIDE];

    char held_buf[512];

    while (1) {
        uint8_t report[20];
        memset(report, 0, sizeof(report));

        r = libusb_control_transfer(
            h,
            0xC0,
            0xC2,
            0x0000,
            0x0000,
            report,
            (uint16_t)sizeof(report),
            1000
        );

        if (r < 0) {
            fprintf(stderr, "\nread control_transfer failed: %d\n", r);
            break;
        }
        if (r < 14) {
            usleep(2000);
            continue;
        }

        // Buttons + triggers
        uint32_t btn = u32le(report, 2);
        uint8_t lt = report[4];
        uint8_t rt = report[5];
        float lt_pct = (lt / 255.0f) * 100.0f;
        float rt_pct = (rt / 255.0f) * 100.0f;

        // Sticks
        int16_t lx = s16le(report, 6);
        int16_t ly = s16le(report, 8);
        int16_t rx = s16le(report, 10);
        int16_t ry = s16le(report, 12);

        float lxn = clampf((float)lx / 32768.0f, -1.0f, 1.0f);
        float lyn = clampf((float)ly / 32768.0f, -1.0f, 1.0f);
        float rxn = clampf((float)rx / 32768.0f, -1.0f, 1.0f);
        float ryn = clampf((float)ry / 32768.0f, -1.0f, 1.0f);

        draw_stick_grid(left_buf, STRIDE, GRID, lxn, lyn, "LEFT", lx, ly);
        draw_stick_grid(right_buf, STRIDE, GRID, rxn, ryn, "RIGHT", rx, ry);

        format_held_buttons(held_buf, sizeof(held_buf), btn);

        // Render
        printf("\033[H"); // home cursor

        printf("Xbox 360 live (vendor poll)  VID=%04x PID=%04x  (Ctrl+C to quit)\n", VID, PID);
        printf("btn=0x%08X  held: %-60s  LT=%3u (%5.1f%%)  RT=%3u (%5.1f%%)\n\n",
               btn, held_buf, lt, lt_pct, rt, rt_pct);

        for (int i = 0; i < 1 + GRID; i++) {
            const char *L = left_buf + i * STRIDE;
            const char *R = right_buf + i * STRIDE;
            printf("%-24s    %-24s\n", L, R);
        }

        fflush(stdout);
        usleep(2000);
    }

    // Show cursor again
    printf("\033[?25h\n");
    fflush(stdout);

    libusb_release_interface(h, ifc);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}