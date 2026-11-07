# FocalTech 2808:c652 USB protocol

Observed on a FocalTech FT9362-family sensor behind a Holtek USB bridge
(`lsusb`: `2808:c652 HOLTEK FocalTech Fingerprint Device`). Everything here was
worked out by probing the device from Linux, and checked against the open-source
[shokerplz/ft9362-driver](https://github.com/shokerplz/ft9362-driver) (2808:0752),
which uses the same framing.

## USB

| | |
|---|---|
| Interface | 0, class 0x0A (CDC Data) |
| Bulk OUT | `0x03`, 64-byte packets |
| Bulk IN | `0x81`, 64-byte packets |
| Kernel driver | none binds; claim interface 0 from userspace (libusb) |

## Framing

Every command and reply uses the same frame:

```
02 | len_hi len_lo | payload (len bytes) | xor
```

`xor` is the XOR of the two length bytes and every payload byte. Replies longer
than 64 bytes arrive split across several bulk IN packets; read until
`3 + len + 1` bytes have arrived.

## Commands

### Finger status

```
send   02 00 03 80 02 01 80
reply  02 00 02 04 FF cs       FF = 01 finger present, 00 none
```

The c652 reply is 6 bytes; the 0752 variant returns 7.

### Read image

```
send   02 00 01 81 80
reply  02 28 01 04 <10240 bytes> cs       (10245 bytes total)
```

The payload is type byte `04` followed by **64 × 80 pixels** (width 64, height
80, row-major), each a little-endian `uint16`. Values span roughly 0–360. The
image is **not encrypted**.

## Firmware behaviour (important)

- The sensor **scans by itself** when touched. The read-image command returns
  the latest stored scan; it does not trigger a new one.
- The "finger present" flag **stays set until the stored scan is read**. If you
  only poll status, it never clears after the first touch. To wait for a lift,
  read and discard the scan while the flag is set.
- No firmware upload or init sequence was needed when booting from Linux.

## Image characteristics

- The 2-pixel border reads dark, and there is a fixed per-column offset pattern
  shared by every frame. Crop the border and subtract column and row medians
  before matching.
- At 64×80 (about 3×4 mm of skin), generic matchers struggle. In our tests,
  SIFT with RANSAC and 40 enrolled touches accepted about 55% of genuine touches
  with zero false accepts out of 40 impostor touches. See `bench/`.
