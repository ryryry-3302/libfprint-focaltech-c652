# Capture frames with no finger present (same protocol as capture.c) and analyse them.
import sys, time, ctypes, numpy as np, cv2
usb = ctypes.CDLL("libusb-1.0.so.0")
usb.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
usb.libusb_init(None)
h = ctypes.c_void_p(usb.libusb_open_device_with_vid_pid(None, 0x2808, 0xc652))
assert h.value, "cannot open sensor"
usb.libusb_set_auto_detach_kernel_driver(h, 1); assert usb.libusb_claim_interface(h, 0) == 0
def bulk(ep, data=None, size=0, to=2000):
    buf = (ctypes.c_ubyte * max(size, len(data or b"")))(*(data or b"")); n = ctypes.c_int()
    r = usb.libusb_bulk_transfer(h, ctypes.c_ubyte(ep), buf, len(buf), ctypes.byref(n), to)
    return bytes(buf[:n.value]) if r == 0 else b""
while bulk(0x81, size=64, to=100): pass
def request(cmd, size):
    bulk(0x03, bytes(cmd)); out = b""
    while len(out) < size:
        c = bulk(0x81, size=size - len(out))
        if not c: break
        out += c
        if len(out) >= 4 and len(out) >= 4 + ((out[1] << 8) | out[2]): break
    return out
frames = []
for i in range(int(sys.argv[2])):
    st = request([2,0,3,0x80,2,1,0x80], 64)
    f = request([2,0,1,0x81,0x80], 10245)
    print(f"frame {i}: status {st.hex(' ')}, {len(f)} bytes")
    if len(f) == 10245: frames.append(np.frombuffer(f[4:-1], "<u2").reshape(80, 64).astype(np.float32))
    time.sleep(0.2)
usb.libusb_release_interface(h, 0)
B = np.array(frames); np.save(sys.argv[1], B)
m = B.mean(0)
print(f"\nblank frames: {len(B)}  mean level {m.mean():.1f}  spatial std of mean frame {m.std():.1f}  temporal noise std {B.std(0).mean():.1f}")
print(f"column-pattern std {m.mean(0).std():.1f}  row-pattern std {m.mean(1).std():.1f}")
u = cv2.resize(cv2.normalize(m, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8), (256, 320), interpolation=cv2.INTER_NEAREST)
cv2.imwrite(sys.argv[1].replace(".npy", ".png"), u)
