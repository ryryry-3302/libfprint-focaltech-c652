"""Evaluate the gallery matcher under realistic use:
enrolment size, multi-try verification and template updating.

Gallery matcher: each enrolled touch keeps its own SIFT features; a probe's
score is the best RANSAC inlier count against any gallery touch.
"""
import random, sys
import numpy as np
sys.argv = ["match.py", "data/dataset"]
exec(open("bench/match.py").read().split("P = {k:")[0])   # prep, sift_feat, sift_score, load

F = lambda lab: [sift_feat(prep(i)) for i in load(lab)]
RI = F("right-index"); ENR_POOL, GEN = RI[20:], RI[:20]
IMP = F("right-middle") + F("left-index") + F("right-thumb") + F("right-ring")

def score(gallery, p): return max((sift_score(g, p) for g in gallery), default=0)

# full score matrices once (probe x enrolled-touch)
Sg = np.array([[sift_score(g, p) for g in ENR_POOL] for p in GEN])
Si = np.array([[sift_score(g, p) for g in ENR_POOL] for p in IMP])
# genuine-vs-genuine (for template update simulation)
Sgg = np.array([[sift_score(a, b) if i != j else 0 for j, b in enumerate(GEN)] for i, a in enumerate(GEN)])
Sig = np.array([[sift_score(g, p) for g in GEN] for p in IMP])

rng = random.Random(0)
print("enrolment size vs single-touch accept (threshold = best impostor + 1, 30 random draws)")
for n in (10, 16, 20, 30, 40):
    taccs, thrs = [], []
    for _ in range(30):
        idx = rng.sample(range(40), n)
        g, i = Sg[:, idx].max(1), Si[:, idx].max(1)
        thr = i.max() + 1; thrs.append(thr); taccs.append(np.mean(g >= thr))
    p = np.mean(taccs)
    print(f"  n={n:2d}: accept {p*100:4.0f}%   3-try {100*(1-(1-p)**3):4.0f}%   (typical threshold {np.median(thrs):.0f} inliers)")

print("\ntemplate update (n=16 enrolled, probes in order; accepted probes join the gallery)")
for margin in (0, 2, 4):
    res = []
    for _ in range(30):
        idx = rng.sample(range(40), 16); order = rng.sample(range(20), 20)
        thr = Si[:, idx].max() + 1          # threshold fixed from enrolment-only impostor scores
        added, acc, imp_hit = [], 0, 0
        for k in order:
            s = max(Sg[k, idx].max(), max((Sgg[k, a] for a in added), default=0))
            if s >= thr:
                acc += 1
                if s >= thr + margin: added.append(k)
        # impostor check against the final grown gallery
        imp = np.maximum(Si[:, idx].max(1), Sig[:, added].max(1) if added else 0)
        res.append((acc / 20, np.sum(imp >= thr)))
    a = np.mean([r[0] for r in res]); fa = np.mean([r[1] for r in res])
    print(f"  update margin {margin}: accept {a*100:4.0f}%   avg impostors accepted after updates {fa:.2f}/40")

print("\nfixed thresholds, n=40 enrolled")
g, i = Sg.max(1), Si.max(1)
print("  impostor best-scores:", sorted(i.tolist())[-8:])
for thr in (5, 6, 7, 8, 10):
    p = np.mean(g >= thr)
    print(f"  thr {thr:2d}: accept {p*100:3.0f}%  3-try {100*(1-(1-p)**3):3.0f}%   impostors {np.sum(i >= thr)}/40")
