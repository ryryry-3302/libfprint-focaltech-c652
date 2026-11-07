# Vendor engine adapter: interface notes

Notes on how `ftWbioEngineAdapter.dll` (FocalTech's Windows Biometric Framework
engine adapter, shipped with the 2808:c652 Windows driver) is driven. They are
enough to run it as a black-box reference matcher under Wine
(`engine-harness/oracle.c`). They come from static analysis with Ghidra 12.1.4
via REA, plus runtime testing. No vendor code is reproduced here, and the DLL is
not distributed.

## Entry point

The only export is `WbioQueryEngineInterface`. It returns a
`WINBIO_ENGINE_INTERFACE` (version 6.0, 41 function slots in Microsoft's
documented order).

## Sensor queries

During `EngineAdapterAttach` the adapter calls `DeviceIoControl` on
`Pipeline->SensorHandle`:

| IOCTL | Reply |
|---|---|
| `0x442018` (sensor size) | 2 bytes: width, height (`64, 80` for c652) |
| app-lock query (during `Activate`) | ignored if zeroed |

Under Wine there is no sensor, so the harness patches the adapter's import of
`KERNEL32!DeviceIoControl` to answer these.

## Sample format (`AcceptSampleData`)

- The sample is a `WINBIO_BIR`. The header block's `BiometricDataFormat` must be
  owner `0x001B`, type `0x0401` (ANSI INCITS 381).
- In the standard data block: pixel depth (offset `0x22`) must be 8, and
  compression (`0x23`) must be 0. The line lengths are at `0x2c`/`0x2e`, and the
  pixels start at `0x38`.
- The adapter copies width × height bytes, using the size from the IOCTL, then
  runs its own quality check (`WINBIO_E_BAD_CAPTURE` on reject).
- Purposes: verify = 1, enroll = 4.

## Internal algorithm object

`EngineContext[0]` is a struct of function pointers; each takes the struct
itself as its first argument. `EngineContext[2]` is the current image
`{u8 w, u8 h, ..., u8 *pixels @+8}`.

| Offset | Role (inferred from call sites) |
|---|---|
| `0x08` | init(cfg, {w, h}) |
| `0x18` | template import(finger=1, data, len) |
| `0x20` | template length(finger=1, &len) |
| `0x28` | template export(finger=1, buf, len) |
| `0x30` | prepare identify(finger=1) |
| `0x38` | enroll image(image, finger=1, index, &info) |
| `0x40` | identify image(image, finger=1, &result). Match if `result[5] > 0` |
| `0x50` | image quality(image, &info) |

Enrolment needs 16 accepted samples. `CommitEnrollment` exports the template
(fixed 1 MiB buffer) and passes it to the storage adapter's `AddRecord`
(slot 9), after `QueryBySubject` (slot 11) reports no existing record.

## Results on our dataset

Enrolling from 40 right-index touches (16 accepted), then verifying 20 held-out
touches and 40 touches from four other fingers:

| 8-bit conversion | Genuine match | False match |
|---|---|---|
| min/max stretch | 12/20 | 0/40 |
| inverted stretch | 12/20 | 0/40 |
| fixed scale 0–400 | 12/20 | 0/40 |
| column-offset removal + stretch | 6/20 | 0/40 |

Single-touch accuracy is about the same as our best open-source matcher (11/20).
On Windows the driver retries capture while the finger stays on the sensor
(`ImageRescanCnt 3`, `NormalImageRescanCnt 5`) and updates templates over time.
Those improve real-world acceptance, independently of the matcher.
