/*
 * libfprint TOD driver for the FocalTech fingerprint sensor, USB 2808:c652.
 *
 * The device delivers raw 64x80 images (see docs/PROTOCOL.md); matching is
 * done on the host by ftmatch (SIFT + RANSAC over a gallery of touches).
 *
 *  - enrol: NR_ENROLL_STAGES separate touches; each touch's features form
 *    the template stored by fprintd (FPI_PRINT_RAW, "fpi-data").
 *  - verify/identify: one touch, best inlier count against every gallery
 *    touch, accepted at >= threshold.
 *  - template update: touches accepted with a clear margin are appended to a
 *    root-only side file (fprintd never re-saves prints after enrolment).
 *
 * USB transfers and feature extraction run in a GTask worker thread so the
 * libfprint main loop stays responsive; all fpi_* calls happen in callbacks.
 */
#define FP_COMPONENT "focaltech_c652"

#include <drivers_api.h>
#include <glib/gstdio.h>
#include <errno.h>
#include "ftmatch.h"

#define EP_OUT 0x03
#define EP_IN  0x81
#define FRAME_LEN (3 + 1 + FT_IMG_W * FT_IMG_H * 2 + 1)

#define NR_ENROLL_STAGES   40     /* more touches = more fingertip coverage */
#define DEFAULT_THRESHOLD  6      /* impostors observed at <= 5 inliers */
#define UPDATE_MARGIN      2      /* learn touches scoring >= threshold + margin */
#define MAX_UPDATES        40
#define DUPLICATE_INLIERS  45     /* enrol touch nearly identical to previous one */
#define MIN_FEATURES       15
#define BLOB_VERSION       1

static const guint8 cmd_status[]  = { 0x02, 0x00, 0x03, 0x80, 0x02, 0x01, 0x80 };
static const guint8 cmd_capture[] = { 0x02, 0x00, 0x01, 0x81, 0x80 };

struct _FpiDeviceFocaltechC652
{
  FpDevice   parent;
  int        threshold;
  int        enroll_stage;
  GPtrArray *enroll_touches;        /* ft_features* */
};

G_DECLARE_FINAL_TYPE (FpiDeviceFocaltechC652, fpi_device_focaltech_c652, FPI,
                      DEVICE_FOCALTECH_C652, FpDevice)
G_DEFINE_TYPE (FpiDeviceFocaltechC652, fpi_device_focaltech_c652, FP_TYPE_DEVICE)

/* ------------------------------------------------------------------ */
/* Gallery = list of touches, serialized as: u32 n, n * (u32 len, data) */

typedef struct
{
  GPtrArray *touches;               /* ft_features* */
  gchar     *update_path;           /* side file with learned touches, may be NULL */
  int        n_base;
} Gallery;

static void
gallery_free (Gallery *g)
{
  g_ptr_array_unref (g->touches);
  g_free (g->update_path);
  g_free (g);
}

static GBytes *
touches_serialize (GPtrArray *touches)
{
  GByteArray *b = g_byte_array_new ();
  guint32 n = touches->len;

  g_byte_array_append (b, (guint8 *) &n, 4);
  for (guint i = 0; i < touches->len; i++)
    {
      guint8 *data;
      guint32 len = ft_serialize (g_ptr_array_index (touches, i), &data);
      g_byte_array_append (b, (guint8 *) &len, 4);
      g_byte_array_append (b, data, len);
      free (data);
    }
  return g_byte_array_free_to_bytes (b);
}

static gboolean
touches_deserialize (const guint8 *data, gsize len, GPtrArray *into)
{
  guint32 n;
  gsize off = 4;

  if (len < 4)
    return FALSE;
  memcpy (&n, data, 4);
  for (guint32 i = 0; i < n; i++)
    {
      guint32 tlen;
      ft_features *f;

      if (off + 4 > len)
        return FALSE;
      memcpy (&tlen, data + off, 4);
      off += 4;
      if (off + tlen > len || !(f = ft_deserialize (data + off, tlen, NULL)))
        return FALSE;
      g_ptr_array_add (into, f);
      off += tlen;
    }
  return TRUE;
}

static const gchar *
state_dir (void)
{
  const gchar *d = g_getenv ("FT_C652_STATE_DIR");   /* override for testing */

  return d ? d : "/var/lib/fprint/focaltech-c652";
}

static Gallery *
gallery_from_print (FpPrint *print)
{
  g_autoptr(GVariant) v = NULL;
  g_autoptr(GVariant) ay = NULL;
  const guint8 *data;
  gsize len;
  guint32 ver;
  Gallery *g;

  g_object_get (print, "fpi-data", &v, NULL);
  if (!v || !g_variant_is_of_type (v, G_VARIANT_TYPE ("(u@ay)")))
    return NULL;
  g_variant_get (v, "(u@ay)", &ver, &ay);
  if (ver != BLOB_VERSION)
    return NULL;
  data = g_variant_get_fixed_array (ay, &len, 1);

  g = g_new0 (Gallery, 1);
  g->touches = g_ptr_array_new_with_free_func ((GDestroyNotify) ft_features_free);
  if (!touches_deserialize (data, len, g->touches))
    {
      gallery_free (g);
      return NULL;
    }
  g->n_base = g->touches->len;

  /* learned touches live next to, not inside, the print */
  g_autofree gchar *hash = g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, len);
  g->update_path = g_strdup_printf ("%s/%s.upd", state_dir (), hash);
  g_autofree guint8 *upd = NULL;
  gsize upd_len;
  if (g_file_get_contents (g->update_path, (gchar **) &upd, &upd_len, NULL))
    if (!touches_deserialize (upd, upd_len, g->touches))
      fp_warn ("ignoring corrupt update file %s", g->update_path);
  return g;
}

static void
gallery_learn (Gallery *g, ft_features *touch)
{
  g_autoptr(GPtrArray) upd = g_ptr_array_new ();
  g_autoptr(GBytes) blob = NULL;
  g_autoptr(GError) err = NULL;
  guint first;

  if (!g->update_path)
    return;
  /* keep the newest MAX_UPDATES learned touches (FIFO) */
  first = g->touches->len - g->n_base >= MAX_UPDATES ? g->n_base + 1 : g->n_base;
  for (guint i = first; i < g->touches->len; i++)
    g_ptr_array_add (upd, g_ptr_array_index (g->touches, i));
  g_ptr_array_add (upd, touch);
  blob = touches_serialize (upd);

  if (g_mkdir_with_parents (state_dir (), 0700) != 0 ||
      !g_file_set_contents_full (g->update_path, g_bytes_get_data (blob, NULL),
                                 g_bytes_get_size (blob),
                                 G_FILE_SET_CONTENTS_CONSISTENT, 0600, &err))
    fp_dbg ("template update not saved: %s", err ? err->message : g_strerror (errno));
  else
    fp_dbg ("template updated (%u learned touches)", upd->len);
}

/* ------------------------------------------------------------------ */
/* USB, run in the worker thread                                        */

static gboolean
request (GUsbDevice *usb, const guint8 *cmd, gsize cmd_len, guint8 *buf, gsize size,
         gsize *got, GCancellable *c, GError **error)
{
  gsize n, total = 0;

  if (!g_usb_device_bulk_transfer (usb, EP_OUT, (guint8 *) cmd, cmd_len, &n, 1000, c, error))
    return FALSE;
  while (total < size)
    {
      if (!g_usb_device_bulk_transfer (usb, EP_IN, buf + total, size - total, &n, 2000, c, error))
        return FALSE;
      if (n == 0)
        break;
      total += n;
      if (total >= 3 && total >= 3 + (gsize) ((buf[1] << 8) | buf[2]) + 1)
        break;
    }
  *got = total;
  return TRUE;
}

static gboolean
frame_ok (const guint8 *b, gsize n)
{
  gsize len;
  guint8 x = 0;

  if (n < 5 || b[0] != 0x02)
    return FALSE;
  len = (b[1] << 8) | b[2];
  if (n < 3 + len + 1)
    return FALSE;
  for (gsize i = 1; i < 3 + len; i++)
    x ^= b[i];
  return x == b[3 + len] && b[3] == 0x04;
}

/* 1 finger present, 0 absent, -1 error */
static int
finger_state (GUsbDevice *usb, GCancellable *c, GError **error)
{
  guint8 b[64];
  gsize n;

  if (!request (usb, cmd_status, sizeof cmd_status, b, sizeof b, &n, c, error))
    return -1;
  return frame_ok (b, n) ? b[4] == 1 : 0;
}

/* Wait for a finger to be lifted, then placed, then read one image. */
static gboolean
capture_touch (GUsbDevice *usb, guint16 *px, GCancellable *c, GError **error)
{
  static guint8 frame[FRAME_LEN + 64];
  int stable = 0, f;
  gsize n;

  /* the firmware latches "finger" until the stored scan is read: discard it */
  while (stable < 8)
    {
      if ((f = finger_state (usb, c, error)) < 0)
        return FALSE;
      if (f == 1 && !request (usb, cmd_capture, sizeof cmd_capture, frame, sizeof frame, &n, c, error))
        return FALSE;
      stable = f == 0 ? stable + 1 : 0;
      g_usleep (50000);
    }
  for (stable = 0; stable < 2; )
    {
      if ((f = finger_state (usb, c, error)) < 0)
        return FALSE;
      stable = f == 1 ? stable + 1 : 0;
      if (stable < 2)
        g_usleep (50000);
    }
  g_usleep (150000);                       /* let the finger settle flat */
  if (!request (usb, cmd_capture, sizeof cmd_capture, frame, sizeof frame, &n, c, error))
    return FALSE;
  if (n != FRAME_LEN || !frame_ok (frame, n))
    {
      g_set_error (error, FP_DEVICE_RETRY, FP_DEVICE_RETRY_GENERAL, "bad image frame (%zu bytes)", n);
      return FALSE;
    }
  for (int k = 0; k < FT_IMG_W * FT_IMG_H; k++)
    px[k] = frame[4 + 2 * k] | (frame[5 + 2 * k] << 8);
  return TRUE;
}

/* ------------------------------------------------------------------ */
/* Scan job: capture + quality + features (+ scores against galleries)  */

typedef struct
{
  GPtrArray   *galleries;           /* Gallery*, may be empty (enrol) */
  int          quality;
  ft_features *feat;
  int          best_gallery;
  int          best_score;
} ScanJob;

static void
scan_job_free (ScanJob *j)
{
  g_ptr_array_unref (j->galleries);
  if (j->feat)
    ft_features_free (j->feat);
  g_free (j);
}

static void
scan_worker (GTask *task, gpointer source, gpointer data, GCancellable *c)
{
  FpDevice *dev = FP_DEVICE (source);
  ScanJob *j = data;
  GError *error = NULL;
  guint16 px[FT_IMG_W * FT_IMG_H];

  if (!capture_touch (fpi_device_get_usb_device (dev), px, c, &error))
    {
      g_task_return_error (task, error);
      return;
    }
  j->quality = ft_image_quality (px);
  if (j->quality == FT_Q_OK)
    {
      j->feat = ft_extract (px);
      if (ft_feature_count (j->feat) < MIN_FEATURES)
        j->quality = FT_Q_FEW_FEATURES;
    }
  j->best_gallery = -1;
  j->best_score = 0;
  if (j->quality == FT_Q_OK)
    for (guint i = 0; i < j->galleries->len; i++)
      {
        Gallery *g = g_ptr_array_index (j->galleries, i);
        for (guint k = 0; k < g->touches->len; k++)
          {
            int s = ft_match_pair (g_ptr_array_index (g->touches, k), j->feat);
            if (s > j->best_score)
              {
                j->best_score = s;
                j->best_gallery = i;
              }
          }
      }
  g_task_return_boolean (task, TRUE);
}

static void
start_scan (FpDevice *dev, GPtrArray *galleries, GAsyncReadyCallback cb)
{
  ScanJob *j = g_new0 (ScanJob, 1);
  GTask *task = g_task_new (dev, fpi_device_get_cancellable (dev), cb, NULL);

  j->galleries = galleries ? galleries : g_ptr_array_new ();
  g_task_set_task_data (task, j, (GDestroyNotify) scan_job_free);
  fpi_device_report_finger_status_changes (dev, FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_NONE);
  g_task_run_in_thread (task, scan_worker);
  g_object_unref (task);
}

static GError *
quality_retry (int quality)
{
  switch (quality)
    {
    case FT_Q_PARTIAL:      return fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER);
    case FT_Q_LOW_CONTRAST: return fpi_device_retry_new (FP_DEVICE_RETRY_REMOVE_FINGER);
    default:                return fpi_device_retry_new (FP_DEVICE_RETRY_GENERAL);
    }
}

/* A scan finished. Returns the job, or NULL after handling a hard error /
 * cancellation (the action has then been completed). Retry-class errors are
 * returned through *retry. */
static ScanJob *
finish_scan (FpDevice *dev, GAsyncResult *res, GError **retry, GError **fatal)
{
  GError *error = NULL;

  fpi_device_report_finger_status_changes (dev, FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_NEEDED);
  if (!g_task_propagate_boolean (G_TASK (res), &error))
    {
      if (error->domain == FP_DEVICE_RETRY)
        *retry = error;
      else
        *fatal = error;
      return NULL;
    }
  ScanJob *j = g_task_get_task_data (G_TASK (res));
  if (j->quality != FT_Q_OK)
    *retry = quality_retry (j->quality);
  return j;
}

/* ------------------------------------------------------------------ */
/* Enrol                                                                */

static void enroll_scan_cb (GObject *src, GAsyncResult *res, gpointer data);

static void
enroll_scan_cb (GObject *src, GAsyncResult *res, gpointer data)
{
  FpDevice *dev = FP_DEVICE (src);
  FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
  GError *retry = NULL, *fatal = NULL;
  ScanJob *j = finish_scan (dev, res, &retry, &fatal);

  if (fatal)
    {
      g_clear_pointer (&self->enroll_touches, g_ptr_array_unref);
      fpi_device_enroll_complete (dev, NULL, fatal);
      return;
    }
  if (!retry && self->enroll_touches->len > 0 &&
      ft_match_pair (g_ptr_array_index (self->enroll_touches, self->enroll_touches->len - 1),
                     j->feat) >= DUPLICATE_INLIERS)
    retry = fpi_device_retry_new_msg (FP_DEVICE_RETRY_CENTER_FINGER,
                                      "Touch a different part of your fingertip");
  if (retry)
    {
      fpi_device_enroll_progress (dev, self->enroll_stage, NULL, retry);
      start_scan (dev, NULL, enroll_scan_cb);
      return;
    }

  g_ptr_array_add (self->enroll_touches, g_steal_pointer (&j->feat));
  self->enroll_stage++;
  fpi_device_enroll_progress (dev, self->enroll_stage, NULL, NULL);
  if (self->enroll_stage < NR_ENROLL_STAGES)
    {
      start_scan (dev, NULL, enroll_scan_cb);
      return;
    }

  FpPrint *print;
  g_autoptr(GBytes) blob = touches_serialize (self->enroll_touches);
  fpi_device_get_enroll_data (dev, &print);
  fpi_print_set_type (print, FPI_PRINT_RAW);
  GVariant *ay = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, g_bytes_get_data (blob, NULL),
                                            g_bytes_get_size (blob), 1);
  g_object_set (print, "fpi-data", g_variant_new ("(u@ay)", BLOB_VERSION, ay), NULL);
  fp_info ("enrolled %u touches, template %zu bytes", self->enroll_touches->len, g_bytes_get_size (blob));
  g_clear_pointer (&self->enroll_touches, g_ptr_array_unref);
  fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
}

static void
dev_enroll (FpDevice *dev)
{
  FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);

  self->enroll_stage = 0;
  g_clear_pointer (&self->enroll_touches, g_ptr_array_unref);
  self->enroll_touches = g_ptr_array_new_with_free_func ((GDestroyNotify) ft_features_free);
  start_scan (dev, NULL, enroll_scan_cb);
}

/* ------------------------------------------------------------------ */
/* Verify / identify                                                    */

static void
match_scan_cb (GObject *src, GAsyncResult *res, gpointer data)
{
  FpDevice *dev = FP_DEVICE (src);
  FpiDeviceFocaltechC652 *self = FPI_DEVICE_FOCALTECH_C652 (dev);
  gboolean identify = fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_IDENTIFY;
  GError *retry = NULL, *fatal = NULL;
  ScanJob *j = finish_scan (dev, res, &retry, &fatal);

  if (fatal)
    {
      if (identify)
        fpi_device_identify_complete (dev, fatal);
      else
        fpi_device_verify_complete (dev, fatal);
      return;
    }
  if (retry)
    {
      if (identify)
        {
          fpi_device_identify_report (dev, NULL, NULL, retry);
          fpi_device_identify_complete (dev, NULL);
        }
      else
        {
          fpi_device_verify_report (dev, FPI_MATCH_ERROR, NULL, retry);
          fpi_device_verify_complete (dev, NULL);
        }
      return;
    }

  gboolean match = j->best_score >= self->threshold;
  fp_dbg ("best score %d (threshold %d) -> %s", j->best_score, self->threshold,
          match ? "match" : "no match");
  if (match && j->best_score >= self->threshold + UPDATE_MARGIN)
    gallery_learn (g_ptr_array_index (j->galleries, j->best_gallery), j->feat);

  if (identify)
    {
      GPtrArray *prints;
      fpi_device_get_identify_data (dev, &prints);
      fpi_device_identify_report (dev, match ? g_ptr_array_index (prints, j->best_gallery) : NULL,
                                  NULL, NULL);
      fpi_device_identify_complete (dev, NULL);
    }
  else
    {
      fpi_device_verify_report (dev, match ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL, NULL, NULL);
      fpi_device_verify_complete (dev, NULL);
    }
}

static void
dev_verify_identify (FpDevice *dev)
{
  GPtrArray *galleries = g_ptr_array_new_with_free_func ((GDestroyNotify) gallery_free);
  /* keep indices aligned with the prints array: unreadable prints get an empty gallery */
  g_autoptr(GPtrArray) single = NULL;
  GPtrArray *prints;

  if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_IDENTIFY)
    {
      fpi_device_get_identify_data (dev, &prints);
    }
  else
    {
      FpPrint *p;
      fpi_device_get_verify_data (dev, &p);
      single = g_ptr_array_new ();
      g_ptr_array_add (single, p);
      prints = single;
    }
  for (guint i = 0; i < prints->len; i++)
    {
      Gallery *g = gallery_from_print (g_ptr_array_index (prints, i));
      if (!g)
        {
          fp_warn ("print %u has no usable template data", i);
          g = g_new0 (Gallery, 1);
          g->touches = g_ptr_array_new ();
        }
      g_ptr_array_add (galleries, g);
    }
  start_scan (dev, galleries, match_scan_cb);
}

/* ------------------------------------------------------------------ */
/* Open / close                                                         */

static void
dev_open (FpDevice *dev)
{
  GError *error = NULL;
  GUsbDevice *usb = fpi_device_get_usb_device (dev);
  guint8 b[64];
  gsize n;

  if (!g_usb_device_reset (usb, &error) ||
      !g_usb_device_claim_interface (usb, 0, 0, &error))
    {
      fpi_device_open_complete (dev, error);
      return;
    }
  while (g_usb_device_bulk_transfer (usb, EP_IN, b, sizeof b, &n, 100, NULL, NULL) && n > 0)
    ;                                     /* drain stale data */
  fpi_device_open_complete (dev, NULL);
}

static void
dev_close (FpDevice *dev)
{
  GError *error = NULL;

  g_usb_device_release_interface (fpi_device_get_usb_device (dev), 0, 0, &error);
  fpi_device_close_complete (dev, error);
}

/* ------------------------------------------------------------------ */

static const FpIdEntry id_table[] = {
  { .vid = 0x2808, .pid = 0xc652 },
  { .vid = 0,      .pid = 0 },
};

static int
read_threshold (void)
{
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  const gchar *env = g_getenv ("FT_C652_THRESHOLD");
  int t;

  if (env && (t = atoi (env)) > 0)
    return t;
  if (g_key_file_load_from_file (kf, "/etc/libfprint-focaltech-c652.conf", 0, NULL) &&
      (t = g_key_file_get_integer (kf, "matcher", "threshold", NULL)) > 0)
    return t;
  return DEFAULT_THRESHOLD;
}

static void
fpi_device_focaltech_c652_init (FpiDeviceFocaltechC652 *self)
{
  self->threshold = read_threshold ();
}

static void
fpi_device_focaltech_c652_finalize (GObject *obj)
{
  g_clear_pointer (&FPI_DEVICE_FOCALTECH_C652 (obj)->enroll_touches, g_ptr_array_unref);
  G_OBJECT_CLASS (fpi_device_focaltech_c652_parent_class)->finalize (obj);
}

static void
fpi_device_focaltech_c652_class_init (FpiDeviceFocaltechC652Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  G_OBJECT_CLASS (klass)->finalize = fpi_device_focaltech_c652_finalize;
  dev_class->id = "focaltech_c652";
  dev_class->full_name = "FocalTech Fingerprint Sensor (2808:c652, open source)";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = NR_ENROLL_STAGES;
  dev_class->temp_hot_seconds = -1;       /* no thermal model needed */

  dev_class->open = dev_open;
  dev_class->close = dev_close;
  dev_class->enroll = dev_enroll;
  dev_class->verify = dev_verify_identify;
  dev_class->identify = dev_verify_identify;

  fpi_device_class_auto_initialize_features (dev_class);
}

/* Entry point looked up by libfprint's TOD loader. */
G_MODULE_EXPORT GType
fpi_tod_shared_driver_get_type (void)
{
  return fpi_device_focaltech_c652_get_type ();
}
