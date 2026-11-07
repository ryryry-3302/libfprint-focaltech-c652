# focaltech-c652

Open-source Linux work for the **FocalTech fingerprint sensor, USB `2808:c652`**
(Holtek bridge, FT9362 family), found in some recent laptops. No Linux driver
supports this sensor out of the box.

**Status:** research / work in progress. Not yet usable for login.

| Part | State |
|---|---|
| USB protocol (status, image capture) | ✅ Documented in [docs/PROTOCOL.md](docs/PROTOCOL.md) |
| Capture tools (`tools/`) | ✅ Working |
| Open-source matcher (`bench/`) | ⚠️ About 55% genuine accept at 0/40 false accept. Not good enough yet |
| Harness for the vendor matcher (`engine-harness/`) | 🚧 Loads under Wine, used as a reference oracle |
| libfprint driver | ⏳ Not started |

## Build

```sh
sudo apt install libusb-1.0-0-dev
make
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
