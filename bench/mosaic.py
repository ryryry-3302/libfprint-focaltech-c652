import sys, glob, cv2, numpy as np
sys.argv = ["match.py", "data/dataset"]
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

# 1. pairwise alignment among enrolled touches
n = len(fE); edges = []
for i in range(n):
    for j in range(i + 1, n):
        (ki, di), (kj, dj) = fE[i], fE[j]
        if di is None or dj is None: continue
        g = ratio_matches(di, dj)
        M, c = similarity(np.float32([ki[m.queryIdx].pt for m in g]), np.float32([kj[m.trainIdx].pt for m in g]))
        if c >= 8: edges.append((c, i, j, M))      # M maps i -> j
print(f"reliable links between enrolled touches: {len(edges)}")

# 2. maximum spanning tree from the best-connected touch; compose transforms into its frame
A = lambda M: np.vstack([M, [0, 0, 1]])
deg = np.zeros(n); [deg.__setitem__(e[1], deg[e[1]] + e[0]) or deg.__setitem__(e[2], deg[e[2]] + e[0]) for e in edges]
root = int(deg.argmax()); G = {root: np.eye(3)}           # G[i] maps touch i -> mosaic frame
changed = True
while changed:
    changed = False
    for c, i, j, M in sorted(edges, key=lambda e: -e[0]):
        if i in G and j not in G: G[j] = G[i] @ np.linalg.inv(A(M)); changed = True
        elif j in G and i not in G: G[i] = G[j] @ A(M); changed = True
print(f"touches stitched into the map: {len(G)}/{n}")

# 3. pooled keypoints in mosaic coordinates
pts, des = [], []
for i, T in G.items():
    k, d = fE[i]
    p = np.float32([kp.pt for kp in k]); p = (T[:2, :2] @ p.T).T + T[:2, 2]
    pts.append(p); des.append(d)
PTS, DES = np.vstack(pts), np.vstack(des)
print(f"map keypoints: {len(PTS)}")

# render mosaic for a sanity check
xy = np.vstack([((T[:2, :2] @ np.float32([[0, 0], [ENR[0][0].shape[1], 0], [0, ENR[0][0].shape[0]], ENR[0][0].shape[::-1]]).T).T + T[:2, 2]) for T in G.values()])
off = -xy.min(0) + 4; W, H = (xy.max(0) + off + 4).astype(int)
acc = np.zeros((H, W), np.float32); wsum = np.zeros((H, W), np.float32)
for i, T in G.items():
    S = T.copy(); S[:2, 2] += off
    nimg, m = ENR[i]
    acc += cv2.warpAffine(nimg, S[:2], (W, H)); wsum += cv2.warpAffine(m, S[:2], (W, H))
mos = acc / np.maximum(wsum, 1e-3)
cv2.imwrite("bench/mosaic.png", cv2.normalize(np.clip(mos, -2.5, 2.5), None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8))
print(f"mosaic size: {W}x{H} px (single touch is {ENR[0][0].shape[1]}x{ENR[0][0].shape[0]}, 2x upscaled)")

# 4. score probes against the map
def map_score(p):
    k, d = sift_feat(p)
    if d is None or len(k) < 3: return 0
    g = ratio_matches(d, DES, pts_b=PTS)
    _, c = similarity(np.float32([k[m.queryIdx].pt for m in g]), np.float32([PTS[m.trainIdx] for m in g]))
    return c
g = np.array([map_score(p) for p in GEN]); i = np.array([map_score(p) for p in IMP])
print(f"\nmap matching: genuine inliers {sorted(g.tolist())}")
print(f"              impostor inliers {sorted(i.tolist())}")
for t in range(int(i.max()), int(i.max()) + 4):
    print(f"  threshold > {t}: accepts {np.mean(g > t)*100:3.0f}% of genuine, {np.sum(i > t)}/40 impostors")
np.savez("bench/scores_map.npz", g=g, i=i)
