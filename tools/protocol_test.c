// SPDX-License-Identifier: LGPL-2.1-or-later
// Exercise the real capture code with a simulated USB transport and clock.
#include <drivers_api.h>
static gboolean mock_transfer(GUsbDevice *, guint8, guint8 *, gsize, gsize *, guint, GCancellable *, GError **);
static void mock_sleep(gulong);
#define g_usb_device_bulk_transfer mock_transfer
#define g_usleep mock_sleep
#include "../src/focaltech_c652.c"

static const int *states;
static size_t state_count, state_index;
static int command, captures, short_write, corrupt_status;
static gulong slept;

static void mock_sleep(gulong us) { slept += us; }
static gboolean mock_transfer(GUsbDevice *usb, guint8 ep, guint8 *buf, gsize len,
                              gsize *got, guint timeout, GCancellable *c, GError **err)
{
  if (c && g_cancellable_set_error_if_cancelled(c, err)) return FALSE;
  if (ep == EP_OUT) {
    command = buf[3]; *got = len - (short_write ? 1 : 0); return TRUE;
  }
  memset(buf, 0, len);
  buf[0] = 2; buf[3] = 4;
  if (command == 0x80) {
    g_assert_cmpuint(state_index, <, state_count);
    buf[2] = 2; buf[4] = states[state_index++];
    buf[5] = 0x02 ^ 0x04 ^ buf[4] ^ (corrupt_status ? 1 : 0);
    *got = 6;
  } else {
    captures++;
    if (len < FRAME_LEN) return FALSE;
    buf[1] = 0x28; buf[2] = 1; buf[FRAME_LEN - 1] = 0x28 ^ 1 ^ 4;
    *got = FRAME_LEN;
  }
  return TRUE;
}

static void run_capture(const int *sequence, size_t n, int expected_captures)
{
  states = sequence; state_count = n; state_index = 0; captures = 0; slept = 0;
  guint16 pixels[FT_IMG_W * FT_IMG_H];
  g_autoptr(GError) err = NULL;
  g_assert_true(capture_touch(NULL, pixels, NULL, &err));
  g_assert_no_error(err);
  g_assert_cmpint(captures, ==, expected_captures);
  g_assert_cmpuint(state_index, ==, n);
}

int main(void)
{
  const int clean[] = {0, 0, 0, 1, 1};
  run_capture(clean, G_N_ELEMENTS(clean), 1);
  // 50 ms release debounce + 50 ms touch debounce + 150 ms settling.
  g_assert_cmpuint(slept, ==, 250000);
  const int latched[] = {1, 1, 0, 0, 0, 1, 1};
  run_capture(latched, G_N_ELEMENTS(latched), 3);
  const int bounce[] = {0, 1, 0, 0, 0, 1, 0, 1, 1};
  run_capture(bounce, G_N_ELEMENTS(bounce), 2);

  guint8 bad[] = {2, 0, 1, 4, 5}; // no finger-state byte
  g_assert_false(frame_ok(bad, sizeof bad));
  guint8 valid[] = {2, 0, 2, 4, 1, 7, 0};
  g_assert_true(frame_ok(valid, 6));
  g_assert_false(frame_ok(valid, 7));
  for (gsize i = 0; i < 6; ++i) g_assert_false(frame_ok(valid, i));

  g_autoptr(GError) err = NULL;
  short_write = 1;
  g_assert_cmpint(finger_state(NULL, NULL, &err), ==, -1);
  g_assert_error(err, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO);
  short_write = 0; g_clear_error(&err);
  states = clean; state_index = 0; state_count = G_N_ELEMENTS(clean); corrupt_status = 1;
  g_assert_cmpint(finger_state(NULL, NULL, &err), ==, -1);
  g_assert_error(err, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO);
  corrupt_status = 0; g_clear_error(&err);

  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  guint16 pixels[FT_IMG_W * FT_IMG_H];
  g_assert_false(capture_touch(NULL, pixels, cancel, &err));
  g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  puts("capture protocol tests passed");
}
