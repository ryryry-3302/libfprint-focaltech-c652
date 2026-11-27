# Capture and matcher validation

This change reduces the capture path's deliberate waits from 600 ms to 250 ms
for a clean lift-and-touch sequence. This is a mocked-clock measurement, not
an end-to-end login benchmark. USB transfer time, daemon startup, matching and
user response time are additional. Latched scans are still discarded before
requiring a fresh touch.

## Matcher experiment

One local session on a FocalTech 2808:c652 sensor in a TECNO MEGABOOK K16SDA:
60 right-index images, 20 right-middle images and 20 left-index images.
Images and derived templates are private and are not included in this change.

The first 40 right-index samples form a fixed gallery. Samples 40–49 are used
for development; 50–59 are held out. Each other finger contributes its first
10 probes to development and last 10 probes to the holdout set. Features are
serialized and deserialized before matching. The driver quality gate and
minimum of 15 probe features are applied when counting accepted probes.
The match threshold stays at 6. Adaptive learning is not simulated.

| Extractor | Development genuine | Held-out genuine | Other-finger accepts (dev + holdout) |
| --- | --- | --- | --- |
| Original SIFT: contrast 0.02, edge 10 | 8/10 | 7/10 | 0/40 |
| Updated SIFT: contrast 0.01, edge 20 | 9/10 | 8/10 | 0/40 |
| Original gallery + updated probes | 8/10 | 7/10 | 0/40 |

The updated extractor retains the original preprocessing and descriptor
representation. Existing prints remain compatible, but re-enrollment is
needed for the measured recognition improvement. RootSIFT and omitting row
correction were also tried on the development set (both 9/10 genuine).
The selected variant keeps the original descriptor representation and showed
stronger genuine-match scores than the original in development. Holdout
scores were inspected after choosing the variant.

This is a small, single-person, single-session experiment. The 0/40 observation
does not establish a population false-accept rate or spoof resistance.
Independent users, other sessions and suspend/resume still need validation.
The updated plugin has been built and tested offline; a successful new live
login with it has not yet been confirmed.

## Reproducing with your own data

Use the existing capture tool with a private working directory and `umask 077`.
The evaluator expects `right-index/00.pgm` through `59.pgm`, and
`right-middle/00.pgm` through `19.pgm` and `left-index/00.pgm` through `19.pgm`.

```sh
meson compile -C build
meson test -C build --print-errorlogs
umask 077
./build/accuracy_eval /path/to/private/dataset > /path/to/private/scores.csv
```

The CSV includes probe quality, feature count, forward and reverse scores,
and the combined time for both matching directions. The actual driver uses
the forward score only. Optional `--export-gallery DIRECTORY` and
`--load-gallery DIRECTORY` flags allow checking compatibility across builds;
these directories contain biometric templates and must remain private.
Use `umask 077` when exporting and do not upload the images, scores or templates.

## Protocol regression tests

`meson test` invokes the actual capture code with mocked USB transfers and
sleep calls. It checks a clean lift, clearing latched scans, bouncing status,
the 250 ms wait total, short command writes, malformed/truncated status,
trailing bytes and cancellation. The test does not open a real USB device.

Build environment: Ubuntu 26.04, GCC 15.2, libfprint TOD 1.95.1,
OpenCV 4.10.0 and Meson 1.10.1.
