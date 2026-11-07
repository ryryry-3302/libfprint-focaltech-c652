import sys, glob, cv2, numpy as np
ARGS = sys.argv[1:]; sys.argv = ["match.py", "data/dataset"] + ARGS
exec(open("bench/match.py").read().split("P = {k:")[0])   # prep, sift_feat, bf
L = lambda lab: [prep(i) for i in load(lab)]
RIp = L("right-index"); ENR, GEN = RIp[20:], RIp[:20]
IMP = L("right-middle") + L("left-index") + L("right-thumb") + L("right-ring")
fE = [sift_feat(p) for p in ENR]

def ratio_matches(da, db, pts_b=None, k=6, ratio=0.8, sep=8):
    """Lowe ratio test; if pts_b given, the 2nd neighbour must be a *different* place
    (>sep px away), since overlapping templates contribute duplicates of one point."""
    out = []
    for nn in bf.knnMatch(da, db, k=k if pts_b is not None else 2):
        if len(nn) < 2: continue
        best = nn[0]; second = None
        for m in nn[1:]:
            if pts_b is None or np.hypot(*(pts_b[m.trainIdx] - pts_b[best.trainIdx])) > sep:
                second = m; break
        if second is None or best.distance < ratio * second.distance: out.append(best)
    return out

def similarity(src, dst):
    if len(src) < 3: return None, 0
    M, inl = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=4, maxIters=4000)
    if M is None: return None, 0
    s = np.hypot(M[0, 0], M[1, 0])
    if not 0.85 < s < 1.15: return None, 0
    return M, int(inl.sum())

LINK = int(ARGS[0]) if ARGS else 6
# 1. pairwise alignment among enrolled touches
n = len(fE); edges = []
for i in range(n):
    for j in range(i + 1, n):
        (ki, di), (kj, dj) = fE[i], fE[j]
        if di is None or dj is None: continue
        g = ratio_matches(di, dj)
        M, c = similarity(np.float32([ki[m.queryIdx].pt for m in g]), np.float32([kj[m.trainIdx].pt for m in g]))
        if c >= LINK: edges.append((c, i, j, M))      # M maps i -> j
print(f"reliable links between enrolled touches: {len(edges)}")


# 2. connected components; each becomes its own map (singletons = single templates)
A = lambda M: np.vstack([M, [0, 0, 1]])
unplaced = set(range(n)); maps = []
while unplaced:
    deg = {i: sum(e[0] for e in edges if i in (e[1], e[2])) for i in unplaced}
    root = max(deg, key=deg.get); G = {root: np.eye(3)}; unplaced.discard(root)
    changed = True
    while changed:
        changed = False
        for c, i, j, M in sorted(edges, key=lambda e: -e[0]):
            if i in G and j in unplaced: G[j] = G[i] @ np.linalg.inv(A(M)); unplaced.discard(j); changed = True
            elif j in G and i in unplaced: G[i] = G[j] @ A(M); unplaced.discard(i); changed = True
    pts, des = [], []
    for i, T in G.items():
        k, d = fE[i]
        if d is None: continue
        p = np.float32([kp.pt for kp in k]); pts.append((T[:2, :2] @ p.T).T + T[:2, 2]); des.append(d)
    if des: maps.append((sorted(G), np.vstack(pts), np.vstack(des)))
sizes = sorted((len(m[0]) for m in maps), reverse=True)
print(f"link threshold {LINK}: {len(edges)} links -> {len(maps)} maps, sizes {sizes}")

def best_score(p):
    k, d = sift_feat(p)
    if d is None or len(k) < 3: return 0
    best = 0
    for _, PTS, DES in maps:
        g = ratio_matches(d, DES, pts_b=PTS)
        _, c = similarity(np.float32([k[m.queryIdx].pt for m in g]), np.float32([PTS[m.trainIdx] for m in g]))
        best = max(best, c)
    return best
g = np.array([best_score(p) for p in GEN]); i = np.array([best_score(p) for p in IMP])
print(f"  genuine  {sorted(g.tolist())}")
print(f"  impostor {sorted(i.tolist())}")
for t in range(int(i.max()), int(i.max()) + 3):
    print(f"  threshold > {t}: accepts {np.mean(g > t)*100:3.0f}% of genuine, {np.sum(i > t)}/40 impostors")
