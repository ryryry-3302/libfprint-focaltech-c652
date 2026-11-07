import sys, glob, cv2, numpy as np
sys.path.insert(0, "bench"); 
root = sys.argv[1]
labs = ["right-index", "right-middle", "left-index"]
ims = {l: [cv2.imread(f, cv2.IMREAD_UNCHANGED).astype(np.float32) for f in sorted(glob.glob(f"{root}/{l}/*.pgm"))] for l in labs}
def hp(im):
    h = im - cv2.GaussianBlur(im, (0, 0), 4)
    return h / (h.std() + 1e-6)
allhp = np.array([hp(i) for l in labs for i in ims[l]])
mean = allhp.mean(0)
print(f"energy of mean high-pass image (pure noise would be ~{1/np.sqrt(len(allhp)):.2f}): {mean.std():.2f}")
print(f"column profile std {mean.mean(0).std():.3f}   row profile std {mean.mean(1).std():.3f}")
# correlation of each image with the shared pattern
for l in labs:
    c = [float((hp(i) * mean).mean() / mean.std()) for i in ims[l]]
    print(f"{l:13s} corr with shared pattern: " + " ".join(f"{x:.2f}" for x in c))
# zero-shift correlation between unrelated fingers (should be ~0 without a fixed pattern)
a = [hp(i) for i in ims["right-middle"]]; b = [hp(i) for i in ims["left-index"]]
z = [float((x * y).mean()) for x in a for y in b]
print(f"zero-shift corr right-middle vs left-index: mean {np.mean(z):.3f}  max {np.max(z):.3f}")
u = cv2.resize(cv2.normalize(mean, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8), (256, 320), interpolation=cv2.INTER_NEAREST)
cv2.imwrite("bench/shared_pattern.png", u)
