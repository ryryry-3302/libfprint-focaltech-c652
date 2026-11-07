import sys, glob, cv2, numpy as np
root = sys.argv[1]
load = lambda lab: [cv2.imread(f, cv2.IMREAD_UNCHANGED).astype(np.float32) for f in sorted(glob.glob(f"{root}/{lab}/*.pgm"))]
RI, RM, LI = load("right-index"), load("right-middle"), load("left-index")

def prep(im):
    """crop edges, remove column/row offsets, upscale 2x, flatten illumination,
    local contrast normalise, finger mask."""
    raw0 = im
    im = im[2:-2, 2:-2].copy()                       # sensor edge pixels read dark
    im -= np.median(im, axis=0, keepdims=True)        # per-column fixed offset
    im -= np.median(im, axis=1, keepdims=True)        # per-row fixed offset
    raw = cv2.resize(raw0[2:-2, 2:-2], None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
    im = cv2.resize(im, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
    bg = cv2.GaussianBlur(im, (0, 0), 8)
    hp = im - bg
    sd = np.sqrt(cv2.GaussianBlur(hp * hp, (0, 0), 8)) + 1e-3
    n = hp / sd
    # mask: where local ridge energy is a decent fraction of the image's typical energy
    sm = cv2.GaussianBlur(raw, (0, 0), 6)
    mask = (sm > 0.5 * np.percentile(sm, 95)).astype(np.float32)   # bright = finger contact
    mask = cv2.erode(mask, np.ones((5, 5), np.uint8))
    return n * mask, mask

# ---- A: correlation with rotation + translation search ----
def corr_score(a, b):
    (na, ma), (nb, mb) = a, b
    h, w = na.shape; c = (w / 2, h / 2); best = -1
    win = cv2.createHanningWindow((w, h), cv2.CV_32F)
    for ang in range(-21, 22, 3):
        R = cv2.getRotationMatrix2D(c, ang, 1.0)
        rb = cv2.warpAffine(nb, R, (w, h)); rm = cv2.warpAffine(mb, R, (w, h))
        (dx, dy), _ = cv2.phaseCorrelate(na * win, rb * win)
        T = np.float32([[1, 0, -dx], [0, 1, -dy]])     # undo b's displacement
        sb = cv2.warpAffine(rb, T, (w, h)); sm = cv2.warpAffine(rm, T, (w, h))
        ov = ma * sm
        if ov.sum() < 0.25 * h * w: continue            # require 25% overlap
        x, y = na[ov > 0.5], sb[ov > 0.5]
        s = float((x * y).sum() / (np.sqrt((x * x).sum() * (y * y).sum()) + 1e-6))
        best = max(best, s)
    return best

# ---- B: SIFT keypoints + RANSAC rigid transform inliers ----
sift = cv2.SIFT_create(contrastThreshold=0.02)
def sift_feat(p):
    n, m = p
    u8 = cv2.normalize(n, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
    return sift.detectAndCompute(u8, (m > 0.5).astype(np.uint8))
bf = cv2.BFMatcher()
def sift_score(fa, fb):
    (ka, da), (kb, db) = fa, fb
    if da is None or db is None or len(ka) < 3 or len(kb) < 3: return 0
    good = [m for m, n in (p for p in bf.knnMatch(da, db, k=2) if len(p) == 2) if m.distance < 0.8 * n.distance]
    if len(good) < 3: return 0
    src = np.float32([ka[m.queryIdx].pt for m in good]); dst = np.float32([kb[m.trainIdx].pt for m in good])
    M, inl = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=4)
    if M is None: return 0
    sc = np.hypot(M[0, 0], M[1, 0])
    if not 0.85 < sc < 1.15: return 0                    # reject implausible scale
    return int(inl.sum())

P = {k: [prep(i) for i in v] for k, v in dict(RI=RI, RM=RM, LI=LI).items()}
F = {k: [sift_feat(p) for p in v] for k, v in P.items()}

def evaluate(name, data, score):
    gen, imp = [], []
    for gal, pro in [(range(0, 10), range(10, 20)), (range(10, 20), range(0, 10))]:
        G = [data["RI"][i] for i in gal]
        for i in pro: gen.append(max(score(g, data["RI"][i]) for g in G))
        for p in data["RM"] + data["LI"]: imp.append(max(score(g, p) for g in G))
    gen, imp = np.array(gen), np.array(imp)
    thr = imp.max()
    print(f"\n{name}")
    print(f"  genuine  scores: min {gen.min():.3f}  median {np.median(gen):.3f}  (n={len(gen)})")
    print(f"  impostor scores: max {imp.max():.3f}  median {np.median(imp):.3f}  (n={len(imp)})")
    print(f"  threshold just above best impostor -> accepts {np.mean(gen > thr)*100:.0f}% of genuine touches, 0/{len(imp)} impostors")
    print(f"  rejected genuine probes: {sorted(np.round(gen[gen <= thr], 3).tolist())}")
    return gen, imp

evaluate("A: correlation + rotation search", P, corr_score)
evaluate("B: SIFT + RANSAC inliers", F, sift_score)
