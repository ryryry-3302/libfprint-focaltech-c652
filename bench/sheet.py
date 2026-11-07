import sys, glob, os, cv2, numpy as np
root = sys.argv[1]
rows = []
for label in ["right-index", "right-middle", "left-index"]:
    files = sorted(glob.glob(f"{root}/{label}/*.pgm"))
    tiles = []
    for f in files:
        im = cv2.imread(f, cv2.IMREAD_UNCHANGED).astype(np.float32)
        sd = im.std()
        im = cv2.normalize(im, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
        im = cv2.resize(im, (128, 160), interpolation=cv2.INTER_NEAREST)
        cv2.putText(im, f"{os.path.basename(f)[:2]} {sd:.0f}", (2, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.35, 255, 1)
        tiles.append(im)
    while len(tiles) < 20: tiles.append(np.zeros((160, 128), np.uint8))
    row = np.hstack(tiles[:10]); rows.append(row)
    if label == "right-index": rows.append(np.hstack(tiles[10:20]))
    print(label, len(files))
cv2.imwrite(sys.argv[2], np.vstack(rows))
