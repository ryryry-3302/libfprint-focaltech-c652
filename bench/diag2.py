import sys, runpy
sys.argv = ["match.py", "data/dataset"]
src = open("bench/match.py").read().split("P = {k:")[0]     # reuse functions only
exec(src)
import numpy as np, cv2
P = {k: [prep(i) for i in v] for k, v in dict(RI=RI, RM=RM, LI=LI).items()}
def detail(a, b):
    (na, ma), (nb, mb) = a, b; h, w = na.shape; c = (w/2, h/2); best = (-1, 0, 0)
    win = cv2.createHanningWindow((w, h), cv2.CV_32F)
    for ang in range(-30, 31, 6):
        R = cv2.getRotationMatrix2D(c, ang, 1.0)
        rb = cv2.warpAffine(nb, R, (w, h)); rm = cv2.warpAffine(mb, R, (w, h))
        (dx, dy), _ = cv2.phaseCorrelate(na * win, rb * win)
        T = np.float32([[1, 0, -dx], [0, 1, -dy]])
        sb = cv2.warpAffine(rb, T, (w, h)); sm = cv2.warpAffine(rm, T, (w, h))
        ov = ma * sm; frac = ov.sum() / (h * w)
        if frac < 0.25: continue
        x, y = na[ov > 0.5], sb[ov > 0.5]
        s = float((x*y).sum() / (np.sqrt((x*x).sum()*(y*y).sum()) + 1e-6))
        if s > best[0]: best = (s, frac, ang)
    return best
rows = []
for gi in range(20):
    for lab in ["RM", "LI"]:
        for pi, p in enumerate(P[lab]):
            s, f, a = detail(P["RI"][gi], p); rows.append((s, f"RI{gi:02d} vs {lab}{pi:02d}", f, a))
rows.sort(reverse=True)
print("top impostor pairs (score, pair, overlap fraction, angle):")
for r in rows[:8]: print(f"  {r[0]:.3f}  {r[1]}  overlap {r[2]:.2f}  angle {r[3]}")
g = []
for i in range(20):
    for j in range(20):
        if i < j: s, f, a = detail(P["RI"][i], P["RI"][j]); g.append((s, f"RI{i:02d} vs RI{j:02d}", f, a))
g.sort(reverse=True)
print("top genuine pairs:")
for r in g[:6]: print(f"  {r[0]:.3f}  {r[1]}  overlap {r[2]:.2f}  angle {r[3]}")
print("mask coverage per LI image:", [round(float(m.mean()), 2) for _, m in P["LI"]])
print("mask coverage per RI image:", [round(float(m.mean()), 2) for _, m in P["RI"]])
