/*
 * capture.c - collect a fingerprint dataset from a FocalTech 2808:c652 sensor.
 *
 *   ./capture <label> <count>      e.g.  ./capture right-index 20
 *
 * For each sample: waits for a finger, captures one image, then waits for the
 * finger to be lifted. Images are saved as dataset/<label>/NN.pgm
 * (64x80, 16-bit big-endian PGM, the sensor's raw values).
 *
 * Protocol (verified on this sensor):
 *   frame  = 02 <len hi> <len lo> <payload> <xor of len+payload>
 *   status = send 02 00 03 80 02 01 80 -> payload 04 <finger 0/1>
 *   image  = send 02 00 01 81 80       -> payload 04 <64*80 uint16 LE>
 *
 * Build: gcc capture.c -o capture -lm $(pkg-config --cflags --libs libusb-1.0)
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libusb.h>

#define VID 0x2808
#define PID 0xc652
#define EP_OUT 0x03
#define EP_IN 0x81
#define W 64
#define H 80
#define FRAME_LEN (3 + 1 + W * H * 2 + 1)

static const unsigned char cmd_status[]  = { 0x02, 0x00, 0x03, 0x80, 0x02, 0x01, 0x80 };
static const unsigned char cmd_capture[] = { 0x02, 0x00, 0x01, 0x81, 0x80 };
static libusb_device_handle *h;

static int frame_ok(const unsigned char *b, int n)
{
    if (n < 5 || b[0] != 0x02) return 0;
    int len = (b[1] << 8) | b[2];
    if (n < 3 + len + 1) return 0;
    unsigned char x = 0;
    for (int i = 1; i < 3 + len; i++) x ^= b[i];
    return x == b[3 + len] && b[3] == 0x04;
}

/* Send cmd, read reply frame (possibly spanning many packets) into buf. */
static int request(const unsigned char *cmd, int cmd_len, unsigned char *buf, int size)
{
    int n, total = 0, r;
    if ((r = libusb_bulk_transfer(h, EP_OUT, (unsigned char *)cmd, cmd_len, &n, 1000))) return -1;
    while (total < size) {
        r = libusb_bulk_transfer(h, EP_IN, buf + total, size - total, &n, 2000);
        if (r || n <= 0) break;
        total += n;
        if (total >= 3 && total >= 3 + ((buf[1] << 8) | buf[2]) + 1) break; /* full frame */
    }
    return total;
}

static int finger(void)
{
    unsigned char b[64];
    int n = request(cmd_status, sizeof cmd_status, b, sizeof b);
    return frame_ok(b, n) ? b[4] == 1 : -1;
}

/* The firmware scans by itself on touch and latches the "finger" flag until the
 * image is read with cmd_capture. So while waiting for a lift, read and discard
 * any pending scan, otherwise the flag stays set forever. */
static void discard_scan(void)
{
    static unsigned char junk[FRAME_LEN + 64];
    request(cmd_capture, sizeof cmd_capture, junk, sizeof junk);
}

static void wait_finger(int want)
{
    int stable = 0;
    while (stable < (want ? 2 : 8)) {   /* debounce: require consecutive readings */
        int f = finger();
        if (!want && f == 1) discard_scan();
        stable = (f == want) ? stable + 1 : 0;
        usleep(50000);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s <label> <count>\n", argv[0]); return 1; }
    const char *label = argv[1];
    int count = atoi(argv[2]);

    char dir[512];
    mkdir("dataset", 0755);
    snprintf(dir, sizeof dir, "dataset/%s", label);
    if (mkdir(dir, 0755) && errno != EEXIST) { perror(dir); return 1; }

    /* continue numbering after existing files */
    int start = 0;
    for (char p[600];; start++) {
        snprintf(p, sizeof p, "%s/%02d.pgm", dir, start);
        if (access(p, F_OK)) break;
    }

    libusb_init(NULL);
    if (!(h = libusb_open_device_with_vid_pid(NULL, VID, PID))) {
        fprintf(stderr, "cannot open sensor 2808:c652\n"); return 1;
    }
    libusb_set_auto_detach_kernel_driver(h, 1);
    if (libusb_claim_interface(h, 0)) { fprintf(stderr, "claim failed\n"); return 1; }
    { unsigned char b[64]; int n; while (!libusb_bulk_transfer(h, EP_IN, b, 64, &n, 100) && n > 0); }

    printf("Capturing %d samples of '%s'. Vary the position/angle slightly each time.\n", count, label);
    if (finger() == 1) { printf("Lift your finger first...\n"); wait_finger(0); }

    static unsigned char frame[FRAME_LEN + 64];
    for (int i = 0; i < count; i++) {
        printf("[%2d/%d] Place finger... ", i + 1, count); fflush(stdout);
        wait_finger(1);
        usleep(150000); /* let the finger settle flat */
        int n = request(cmd_capture, sizeof cmd_capture, frame, sizeof frame);
        if (n != FRAME_LEN || !frame_ok(frame, n)) {
            printf("bad frame (%d bytes), retrying this sample\n", n);
            i--; wait_finger(0); continue;
        }
        /* quality stats: mean / stddev of the image */
        double sum = 0, sq = 0;
        unsigned short px[W * H];
        for (int k = 0; k < W * H; k++) {
            px[k] = frame[4 + 2 * k] | (frame[5 + 2 * k] << 8);
            sum += px[k]; sq += (double)px[k] * px[k];
        }
        double mean = sum / (W * H), sd = sqrt(sq / (W * H) - mean * mean);

        char path[600];
        snprintf(path, sizeof path, "%s/%02d.pgm", dir, start + i);
        FILE *f = fopen(path, "wb");
        fprintf(f, "P5\n%d %d\n65535\n", W, H);
        for (int k = 0; k < W * H; k++) { fputc(px[k] >> 8, f); fputc(px[k] & 0xff, f); }
        fclose(f);
        printf("captured (mean %.0f, contrast %.0f)%s. Lift finger.\n", mean, sd,
               sd < 30 ? "  <- low contrast, partial touch?" : "");
        wait_finger(0);
    }
    printf("Done. Saved to %s/\n", dir);
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
