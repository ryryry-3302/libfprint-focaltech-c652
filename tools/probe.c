// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * probe.c - check whether a FocalTech 2808:c652 sensor speaks the FT9362
 * protocol used by the open-source shokerplz/ft9362-driver (2808:0752).
 *
 *   ./probe           send the "finger present?" status command 5 times, print replies
 *   ./probe capture   wait for a finger, then request one image, save it to capture.raw
 *
 * It only sends the two commands below. It does not write firmware or settings.
 * Build: gcc probe.c -o probe $(pkg-config --cflags --libs libusb-1.0)
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>

#define VID     0x2808
#define PID     0xc652
#define EP_OUT  0x03
#define EP_IN   0x81

static const unsigned char cmd_status_poll[] = { 0x02, 0x00, 0x03, 0x80, 0x02, 0x01, 0x80 };
static const unsigned char cmd_capture[]     = { 0x02, 0x00, 0x01, 0x81, 0x80 };

static void hexdump(const char *label, const unsigned char *b, int n)
{
    printf("%s (%d bytes):", label, n);
    for (int i = 0; i < n && i < 64; i++) printf(" %02x", b[i]);
    printf(n > 64 ? " ...\n" : "\n");
}

/* Send a command, read one reply packet. Returns reply length or -1. */
static int xfer(libusb_device_handle *h, const unsigned char *cmd, int len,
                unsigned char *reply, int reply_size)
{
    int n = 0, r;
    r = libusb_bulk_transfer(h, EP_OUT, (unsigned char *)cmd, len, &n, 1000);
    if (r) { printf("  write failed: %s\n", libusb_error_name(r)); return -1; }
    r = libusb_bulk_transfer(h, EP_IN, reply, reply_size, &n, 1000);
    if (r) { printf("  read failed: %s\n", libusb_error_name(r)); return -1; }
    return n;
}

/* Frame: 02 <len hi> <len lo> <payload: len bytes> <xor of len+payload bytes>.
 * Returns 1 if the frame is well formed. */
static int frame_ok(const unsigned char *b, int n)
{
    if (n < 4 || b[0] != 0x02) return 0;
    int len = (b[1] << 8) | b[2];
    if (n < 3 + len + 1) return 0;
    unsigned char x = 0;
    for (int i = 1; i < 3 + len; i++) x ^= b[i];
    return x == b[3 + len];
}

/* Status reply payload: 04 <finger> [...] -> returns 1/0, or -1 if unrecognised */
static int finger_state(const unsigned char *b, int n)
{
    if (frame_ok(b, n) && b[3] == 0x04) return b[4] == 0x01;
    return -1;
}

int main(int argc, char **argv)
{
    int capture = argc > 1 && !strcmp(argv[1], "capture");
    unsigned char buf[64];
    int r, n;

    if (libusb_init(NULL)) { puts("libusb_init failed"); return 1; }
    libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, VID, PID);
    if (!h) { puts("Could not open 2808:c652 (not present, or need sudo)"); return 1; }
    libusb_set_auto_detach_kernel_driver(h, 1);
    if ((r = libusb_claim_interface(h, 0))) {
        printf("claim interface failed: %s\n", libusb_error_name(r)); return 1;
    }

    /* Drain any stale data the sensor might have queued. */
    while (libusb_bulk_transfer(h, EP_IN, buf, sizeof buf, &n, 100) == 0 && n > 0)
        hexdump("stale", buf, n);

    puts("== status poll ==");
    int recognised = 0;
    for (int i = 0; i < 5; i++) {
        n = xfer(h, cmd_status_poll, sizeof cmd_status_poll, buf, sizeof buf);
        if (n < 0) break;
        hexdump("reply", buf, n);
        int f = finger_state(buf, n);
        if (f >= 0) recognised++;
        printf("  -> %s\n", f < 0 ? "unrecognised format" : f ? "FT9362 format, finger ON" : "FT9362 format, no finger");
        usleep(200000);
    }
    printf("\nVerdict: %s\n", recognised ? "sensor answers in FT9362 protocol format"
                                         : "replies do NOT match FT9362 format");

    if (capture && recognised) {
        puts("\n== capture: place your finger on the sensor (120 s) ==");
        fflush(stdout);
        int on = 0;
        unsigned char last[64] = {0};
        int last_n = 0;
        for (int i = 0; i < 2400 && !on; i++) {
            n = xfer(h, cmd_status_poll, sizeof cmd_status_poll, buf, sizeof buf);
            if (n > 0 && (n != last_n || memcmp(buf, last, n))) {   /* show changes */
                hexdump("  status changed", buf, n);
                memcpy(last, buf, n); last_n = n;
            }
            on = n > 0 && finger_state(buf, n) == 1;
            if (!on) usleep(50000);
        }
        if (!on) { puts("no finger detected, giving up"); goto out; }

        static unsigned char img[32768];
        int total = 0;
        r = libusb_bulk_transfer(h, EP_OUT, (unsigned char *)cmd_capture, sizeof cmd_capture, &n, 1000);
        if (r) { printf("capture write failed: %s\n", libusb_error_name(r)); goto out; }
        /* Read until the sensor stops sending (timeout) or buffer full. */
        while (total < (int)sizeof img &&
               libusb_bulk_transfer(h, EP_IN, img + total, sizeof img - total, &n, 2000) == 0 && n > 0)
            total += n;
        hexdump("image start", img, total);
        printf("image bytes received: %d (FT9362/0752 expects 12166)\n", total);
        FILE *f = fopen("capture.raw", "wb");
        if (f) { fwrite(img, 1, total, f); fclose(f); puts("saved to capture.raw"); }
    }
out:
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
