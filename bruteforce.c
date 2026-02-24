// x360_bruteforce.c
// Build: gcc -O2 -Wall x360_bruteforce.c -lusb-1.0 -o x360_bruteforce
//
// Usage examples:
//   sudo ./x360_bruteforce
//   sudo ./x360_bruteforce --req 0x00 0xff --val 0x0000 0x00ff --idx 0x0000 0x000f --sleep-us 5000
//
// What it does:
//   - (optional) sends the known arm command (0x40, req=0x48, val=0x0006, idx=0)
//   - reads baseline from the known poll (0xC0, req=0xC2, 20 bytes)
//   - iterates OUT vendor/device control transfers and after each one polls again
//   - prints when the poll response changes
//
// Notes:
//   - Keep ranges SMALL at first.
//   - Some values can hang the device until replug.
//   - Use sudo or a udev rule.

#include <libusb-1.0/libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static const uint16_t VID = 0x045E;
static const uint16_t PID = 0x028F;

static void hexprint(const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
}

static int parse_u32_hex(const char *s, uint32_t *out) {
    if (!s || !out) return 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0); // accepts 0x... or decimal
    if (end == s || *end != '\0') return 0;
    *out = (uint32_t)v;
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --no-arm                 Do not send the known arm cmd (req=0x48 val=0x0006)\n"
        "  --req  <start> <end>     bRequest range (default 0x00 0xff)\n"
        "  --val  <start> <end>     wValue range   (default 0x0000 0x00ff)\n"
        "  --idx  <start> <end>     wIndex range   (default 0x0000 0x000f)\n"
        "  --len  <n>               OUT data length (default 0)\n"
        "  --pat  <0|1|2|3>         OUT data pattern: 0=00.. 1=ff.. 2=inc 3=req^i (default 2)\n"
        "  --sleep-us <n>           microseconds between tries (default 2000)\n",
        argv0
    );
}

int main(int argc, char **argv) {
    int do_arm = 1;

    uint32_t req_s = 0x00,   req_e = 0xff;
    uint32_t val_s = 0x0000, val_e = 0x00ff;
    uint32_t idx_s = 0x0000, idx_e = 0x000f;

    uint32_t out_len = 0;
    uint32_t pattern = 2;
    uint32_t sleep_us = 10000000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-arm")) {
            do_arm = 0;
        } else if (!strcmp(argv[i], "--req") && i + 2 < argc) {
            if (!parse_u32_hex(argv[i+1], &req_s) || !parse_u32_hex(argv[i+2], &req_e)) { usage(argv[0]); return 2; }
            i += 2;
        } else if (!strcmp(argv[i], "--val") && i + 2 < argc) {
            if (!parse_u32_hex(argv[i+1], &val_s) || !parse_u32_hex(argv[i+2], &val_e)) { usage(argv[0]); return 2; }
            i += 2;
        } else if (!strcmp(argv[i], "--idx") && i + 2 < argc) {
            if (!parse_u32_hex(argv[i+1], &idx_s) || !parse_u32_hex(argv[i+2], &idx_e)) { usage(argv[0]); return 2; }
            i += 2;
        } else if (!strcmp(argv[i], "--len") && i + 1 < argc) {
            if (!parse_u32_hex(argv[i+1], &out_len)) { usage(argv[0]); return 2; }
            i += 1;
        } else if (!strcmp(argv[i], "--pat") && i + 1 < argc) {
            if (!parse_u32_hex(argv[i+1], &pattern)) { usage(argv[0]); return 2; }
            i += 1;
        } else if (!strcmp(argv[i], "--sleep-us") && i + 1 < argc) {
            if (!parse_u32_hex(argv[i+1], &sleep_us)) { usage(argv[0]); return 2; }
            i += 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

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
        fprintf(stderr, "claim_interface(%d) failed: %d (try sudo/udev)\n", ifc, r);
        libusb_close(h);
        libusb_exit(ctx);
        return 1;
    }

    // Optional: your known "arm" command
    if (do_arm) {

        r = libusb_control_transfer(
            h,
            0x40,   // OUT | Vendor | Device
            0x48,   // bRequest
            0x0006, // wValue
            0x0000, // wIndex
            NULL,
            0,
            1000
        );
        if (r < 0) {
            fprintf(stderr, "arm transfer failed: %d\n", r);
            // continue anyway
        } else {
            printf("Armed (req=0x48 val=0x0006 idx=0x0000)\n");
        }
    }

    // Baseline poll
    uint8_t last_poll[20];
    memset(last_poll, 0, sizeof(last_poll));

    r = libusb_control_transfer(
        h,
        0xC0,      // IN | Vendor | Device
        0xC2,      // bRequest (known poll)
        0x0000,
        0x0000,
        last_poll,
        (uint16_t)sizeof(last_poll),
        1000
    );
    if (r < 0) {
        fprintf(stderr, "initial poll failed: %d\n", r);
        // continue anyway; last_poll stays zeros
    } else {
        printf("Baseline poll (%d bytes): ", r);
        hexprint(last_poll, r);
        printf("\n");
    }

    uint8_t *out_buf = NULL;
    if (out_len > 0) {
        out_buf = (uint8_t*)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "malloc failed\n");
            libusb_release_interface(h, ifc);
            libusb_close(h);
            libusb_exit(ctx);
            return 1;
        }
    }

    printf("Bruteforcing: req[%#x..%#x] val[%#x..%#x] idx[%#x..%#x] len=%u pat=%u sleep_us=%u\n",
           (unsigned)req_s, (unsigned)req_e,
           (unsigned)val_s, (unsigned)val_e,
           (unsigned)idx_s, (unsigned)idx_e,
           (unsigned)out_len, (unsigned)pattern, (unsigned)sleep_us);

    // Main brute loop
    for (uint32_t req = req_s; req <= req_e; req++) {
        for (uint32_t val = val_s; val <= val_e; val++) {
            for (uint32_t idx = idx_s; idx <= idx_e; idx++) {

                // Fill OUT buffer (if used)
                if (out_buf) {
                    for (uint32_t i = 0; i < out_len; i++) {
                        switch (pattern) {
                            case 0: out_buf[i] = 0x00; break;
                            case 1: out_buf[i] = 0xFF; break;
                            case 2: out_buf[i] = (uint8_t)i; break;
                            case 3: out_buf[i] = (uint8_t)(req ^ i); break;
                            default: out_buf[i] = (uint8_t)i; break;
                        }
                    }
                }

                // Send candidate OUT vendor/device control transfer
                printf("TRY req=%02x val=%04x idx=%04x len=%u\n",(unsigned)req,(unsigned)val,(unsigned)idx,(unsigned)out_len);
                fflush(stdout);
                r = libusb_control_transfer(
                    h,
                    0x40,                  // OUT | Vendor | Device
                    (uint8_t)req,
                    (uint16_t)val,
                    (uint16_t)idx,
                    out_buf,
                    (uint16_t)out_len,
                    200 // keep short; device can stall
                );

                if (r < 0) {
                    // Many combos will stall/error. Don’t spam the terminal.
                    // Uncomment if you want to see all errors:
                    // printf("ERR req=%02x val=%04x idx=%04x -> %d\n", req, val, idx, r);
                    usleep(sleep_us);
                    continue;
                }

                // Poll after the OUT to detect any observable change
                uint8_t poll[20];
                memset(poll, 0, sizeof(poll));
                int pr = libusb_control_transfer(
                    h,
                    0xC0,
                    0xC2,
                    0x0000,
                    0x0000,
                    poll,
                    (uint16_t)sizeof(poll),
                    200
                );

                if (pr > 0 && memcmp(poll, last_poll, (size_t)pr) != 0) {
                    printf("HIT: OUT req=%02x val=%04x idx=%04x len=%u (sent=%d) poll=%d\n    old=",
                           (unsigned)req, (unsigned)val, (unsigned)idx, (unsigned)out_len, r, pr);
                    hexprint(last_poll, pr);
                    printf("\n    new=");
                    hexprint(poll, pr);
                    printf("\n");
                    memcpy(last_poll, poll, (size_t)pr);
                }

                usleep(sleep_us);
            }
        }
    }

    free(out_buf);
    libusb_release_interface(h, ifc);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}