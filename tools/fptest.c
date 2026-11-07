/*
 * fptest - exercise the driver through the public libfprint API,
 * without fprintd (useful before installing).
 *
 *   FP_TOD_DRIVERS_DIR=build ./build/fptest enroll print.bin
 *   FP_TOD_DRIVERS_DIR=build ./build/fptest verify print.bin [tries]
 */
#include <fprint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void enroll_progress (FpDevice *dev, gint stage, FpPrint *print G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED, GError *error)
{
  if (error)
    printf ("  retry: %s\n", error->message);
  else
    printf ("  touch %d/%d accepted. Lift, then touch a slightly different spot.\n",
            stage, fp_device_get_nr_enroll_stages (dev));
  fflush (stdout);
}

int main (int argc, char **argv)
{
  g_autoptr(GError) err = NULL;
  if (argc < 3 || (strcmp (argv[1], "enroll") && strcmp (argv[1], "verify")))
    {
      fprintf (stderr, "usage: %s enroll|verify <print file> [tries]\n", argv[0]);
      return 1;
    }
  g_autoptr(FpContext) ctx = fp_context_new ();
  GPtrArray *devs = fp_context_get_devices (ctx);
  FpDevice *dev = NULL;
  for (guint i = 0; i < devs->len; i++)
    if (!strcmp (fp_device_get_driver (g_ptr_array_index (devs, i)), "focaltech_c652"))
      dev = g_ptr_array_index (devs, i);
  if (!dev)
    {
      fprintf (stderr, "focaltech_c652 device not found (is FP_TOD_DRIVERS_DIR set?)\n");
      return 1;
    }
  printf ("device: %s\n", fp_device_get_name (dev));
  if (!fp_device_open_sync (dev, NULL, &err))
    {
      fprintf (stderr, "open: %s\n", err->message);
      return 1;
    }

  if (!strcmp (argv[1], "enroll"))
    {
      FpPrint *tmpl = fp_print_new (dev);
      fp_print_set_finger (tmpl, FP_FINGER_RIGHT_INDEX);
      printf ("Enrolling: touch the sensor %d times, moving around your fingertip.\n",
              fp_device_get_nr_enroll_stages (dev));
      g_autoptr(FpPrint) print = fp_device_enroll_sync (dev, tmpl, NULL, enroll_progress, NULL, &err);
      if (!print)
        {
          fprintf (stderr, "enroll: %s\n", err->message);
          return 1;
        }
      guchar *data; gsize len;
      fp_print_serialize (print, &data, &len, &err);
      g_file_set_contents (argv[2], (gchar *) data, len, &err);
      printf ("saved %s (%zu bytes)\n", argv[2], len);
      g_free (data);
    }
  else
    {
      gchar *data; gsize len;
      if (!g_file_get_contents (argv[2], &data, &len, &err))
        {
          fprintf (stderr, "%s\n", err->message);
          return 1;
        }
      g_autoptr(FpPrint) print = fp_print_deserialize ((guchar *) data, len, &err);
      g_free (data);
      if (!print)
        {
          fprintf (stderr, "deserialize: %s\n", err->message);
          return 1;
        }
      int tries = argc > 3 ? atoi (argv[3]) : 1, ok = 0, done = 0;
      while (done < tries)
        {
          gboolean match = FALSE;
          g_clear_error (&err);
          printf ("[%d/%d] touch the sensor... ", done + 1, tries);
          fflush (stdout);
          if (!fp_device_verify_sync (dev, print, NULL, NULL, NULL, &match, NULL, &err))
            {
              if (err->domain == FP_DEVICE_RETRY)
                {
                  printf ("retry (%s)\n", err->message);
                  continue;
                }
              fprintf (stderr, "verify: %s\n", err->message);
              return 1;
            }
          printf ("%s\n", match ? "MATCH" : "no match");
          ok += match; done++;
        }
      printf ("matched %d/%d\n", ok, tries);
    }
  fp_device_close_sync (dev, NULL, NULL);
  return 0;
}
