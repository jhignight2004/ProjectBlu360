// x360_send.c
// Build: gcc -O2 -Wall x360_send.c -lusb-1.0 -o x360_send
// Usage: sudo ./x360_send 0x47 0x0000 0x0000 [count] [delay_us]
// Example: sudo ./x360_send 0x47 0 0 1
//          sudo ./x360_send 0x47 0 0 50 100000   (50 times @100ms)

#include <libusb-1.0/libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint16_t VID = 0x045E;
static const uint16_t PID = 0x028F;

static uint32_t parse(const char *s) { return (uint32_t)strtoul(s, NULL, 0); }

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <req> <wValue> <wIndex> [count] [delay_us]\n", argv[0]);
        return 2;
    }

    uint8_t  req = (uint8_t)parse(argv[1]);
    uint16_t val = (uint16_t)parse(argv[2]);
    uint16_t idx = (uint16_t)parse(argv[3]);
    int count = (argc >= 5) ? (int)parse(argv[4]) : 1;
    int delay_us = (argc >= 6) ? (int)parse(argv[5]) : 0;

    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    if (libusb_init(&ctx) != 0) return 1;
    h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) { fprintf(stderr, "Device not found\n"); libusb_exit(ctx); return 1; }

    int ifc = 0;
    if (libusb_kernel_driver_active(h, ifc) == 1) (void)libusb_detach_kernel_driver(h, ifc);
    (void)libusb_set_configuration(h, 1);
    if (libusb_claim_interface(h, ifc) != 0) { fprintf(stderr, "claim failed\n"); goto out; }

    // Optional: arm streaming (comment out if you don't want it)
    (void)libusb_control_transfer(h, 0x40, 0x48, 0x0006, 0x0000, NULL, 0, 1000);

    for (int i = 0; i < count; i++) {
        int r = libusb_control_transfer(h, 0x40, req, val, idx, NULL, 0, 200);
        printf("OUT 0x40 req=%02x val=%04x idx=%04x -> %d\n", req, val, idx, r);
        if (delay_us > 0) usleep(delay_us);
    }

out:
    libusb_release_interface(h, ifc);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}