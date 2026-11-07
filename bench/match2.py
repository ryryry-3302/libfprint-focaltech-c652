import sys, glob, cv2, numpy as np
sys.argv = ["match.py", "data/dataset"]
exec(open("bench/match.py").read().split("P = {k:")[0])   # prep, corr_score, sift_feat, sift_score
L = lambda lab: [prep(i) for i in load(lab)]
RIp = L("right-index")
ENR, GEN = RIp[20:], RIp[:20]
IMP = L("right-middle") + L("left-index") + L("right-thumb") + L("right-ring")
fE = [sift_feat(p) for p in ENR]; fG = [sift_feat(p) for p in GEN]; fI = [sift_feat(p) for p in IMP]

def scores(score, E, probes):
    return np.array([[score(e, p) for e in E] for p in probes])   # probes x templates

def report(name, g, i):
    thr = i.max()
    acc0 = np.mean(g > thr) * 100
    # also: accept rate if we tolerate 1 of 40 impostors (2.5%)
    thr1 = np.sort(i)[-2]; acc1 = np.mean(g > thr1) * 100
    print(f"  {name:28s} genuine med {np.median(g):6.3f} | impostor max {i.max():6.3f} | accept @0 FA: {acc0:3.0f}%  @1/40 FA: {acc1:3.0f}%")

import time; t = time.time()
Sg_c, Si_c = scores(corr_score, ENR, GEN), scores(corr_score, ENR, IMP)
Sg_s, Si_s = scores(sift_score, fE, fG), scores(sift_score, fE, fI)
print(f"(scored in {time.time()-t:.0f}s)")
np.savez("bench/scores2.npz", Sg_c=Sg_c, Si_c=Si_c, Sg_s=Sg_s, Si_s=Si_s)
for k in (1, 3, 5):
    top = lambda S: np.sort(S, 1)[:, -k:].mean(1)
    print(f"top-{k} fusion:")
    report("correlation", top(Sg_c), top(Si_c))
    report("SIFT inliers", top(Sg_s), top(Si_s))
