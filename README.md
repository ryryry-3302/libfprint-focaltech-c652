# focaltech-c652

Open-source Linux work for the **FocalTech fingerprint sensor, USB `2808:c652`**
(Holtek bridge, FT9362 family), found in some recent laptops. No Linux driver
supports this sensor out of the box.

**Status:** working libfprint driver (v0.1). Usable for fingerprint login and `sudo`.

| Part | State |
|---|---|
| USB protocol (status, image capture) | ✅ Documented in [docs/PROTOCOL.md](docs/PROTOCOL.md) |
| Capture tools (`tools/`) | ✅ Working |
| Open-source matcher (`bench/`) | ⚠️ About 55% genuine accept at 0/40 false accept. Not good enough yet |
| Vendor matcher under Wine (`engine-harness/`) | ✅ Reference oracle: 60% genuine at 0/40 false. See [docs/ENGINE.md](docs/ENGINE.md) |
| libfprint driver (`src/focaltech_c652.c`) | ✅ Works with fprintd: 65% per touch, 0/10 false. Improves as it learns |

## Install (Ubuntu 24.04)

```sh
git clone <this repo> && cd <repo>
./scripts/install.sh                     # builds and installs the libfprint-TOD plugin
fprintd-enroll -f right-index-finger     # 40 touches: move around your fingertip
fprintd-verify
sudo pam-auth-update --enable fprintd    # fingerprint for login and sudo
```

The driver is a [libfprint-TOD](https://launchpad.net/ubuntu/+source/libfprint) plugin. It does not
replace any system package. To remove it: `fprintd-delete "$USER"`, then `./scripts/uninstall.sh`.

### How it works

- **Enrolment:** 40 touches, each stored as SIFT features (never images). Touches
  that are too similar to the previous one are rejected, so you cover more of
  your fingertip.
- **Matching:** RANSAC alignment against every enrolled touch. A match needs at
  least 6 inliers (wrong fingers scored at most 5 in testing). Configure this in
  `/etc/libfprint-focaltech-c652.conf`.
- **Learning:** touches that match with a clear margin (score at least 8) are
  added to a root-only side file (`/var/lib/fprint/focaltech-c652/`, up to 40),
  so acceptance improves with use.
- **Security:** a 64×80 sensor is convenience-grade. Our tests had 0 false
  accepts out of 50 wrong-finger touches, but that is a small sample.

### Measured (one user)

| Test | Result |
|---|---|
| Same finger, single touch, fresh enrolment | 13/20 (last 6 in a row matched as it learned) |
| Other fingers | 0/10 live, 0/40 offline |
| Within 3 tries (Ubuntu's default) | about 96% expected |

## Development build

```sh
sudo apt install libusb-1.0-0-dev libfprint-2-tod-dev libfprint-2-dev libopencv-dev meson ninja-build
make                      # USB research tools
meson setup build && ninja -C build
FP_TOD_DRIVERS_DIR=$PWD/build FT_C652_STATE_DIR=$PWD/data/state ./build/fptest enroll data/print.bin
```

## Access without root

```sh
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2808", ATTRS{idProduct}=="c652", TAG+="uaccess"' \
  | sudo tee /etc/udev/rules.d/70-focaltech-c652.rules
sudo udevadm control --reload && sudo udevadm trigger
```

## Tools

- `tools/probe`: sends the status command and prints the replies.
  `tools/probe capture` saves one image to `capture.raw`.
- `tools/capture <label> <count>`: collects a dataset into
  `data/dataset/<label>/NN.pgm` (64×80, 16-bit PGM).

## Matching research

`bench/*.py` evaluate matchers on a local dataset (system Python with numpy and
OpenCV). Captured fingerprints are biometric data, so `data/` is git-ignored and
never published. Collect your own with `tools/capture`.

## Vendor matcher harness

`engine-harness/` contains a small Windows program, built with mingw-w64 and run
under Wine. It loads FocalTech's WinBio engine adapter, so its results can serve
as a reference while developing an open replacement. **No vendor files are
distributed here.** Supply your own copy of the Windows driver package in
`vendor/` (git-ignored).

```sh
make harness
python3 engine-harness/make_reg.py vendor/FtWbioDriverUmdfV3.inf engine-harness/focal.reg
```

## Disclaimer

Not affiliated with FocalTech or Holtek. A sensor this small gives
convenience-level security, not protection against a determined attacker.
