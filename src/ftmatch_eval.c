/*
 * ftmatch_eval - check the C matcher against the Python bench on a local
 * dataset (data/dataset/<label>/NN.pgm).
 *
 *   ftmatch_eval <dataset dir> <threshold>
 *
 * Enrols right-index 20..59 (through serialize/deserialize, as the driver
 * will), verifies right-index 00..19 and all touches of the other fingers.
 */
#include "ftmatch.h"
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_pgm(const char *path, uint16_t *px)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int w, h, mx;
    if (fscanf(f, "P5 %d %d %d", &w, &h, &mx) != 3 || w != FT_IMG_W || h != FT_IMG_H) { fclose(f); return -1; }
    fgetc(f);
    for (int i = 0; i < w * h; i++) { int a = fgetc(f), b = fgetc(f); px[i] = (uint16_t)((a << 8) | b); }
    fclose(f); return 0;
}

static ft_features *roundtrip(ft_features *f)
{
    uint8_t *blob; size_t len = ft_serialize(f, &blob), used;
    ft_features *g = ft_deserialize(blob, len, &used);
    free(blob); ft_features_free(f); return g;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s <dataset> <threshold>\n", argv[0]); return 1; }
    int thr = atoi(argv[2]);
    char pat[512]; glob_t g; uint16_t px[FT_IMG_W * FT_IMG_H];

    snprintf(pat, sizeof pat, "%s/right-index/*.pgm", argv[1]);
    glob(pat, 0, NULL, &g);
    if (g.gl_pathc < 60) { fprintf(stderr, "need 60 right-index images\n"); return 1; }
    ft_features *gal[40]; size_t tpl_bytes = 0;
    for (int i = 0; i < 40; i++) {
        load_pgm(g.gl_pathv[20 + i], px);
        gal[i] = roundtrip(ft_extract(px));
        uint8_t *b; tpl_bytes += ft_serialize(gal[i], &b); free(b);
    }
    printf("template: 40 touches, %zu bytes\n", tpl_bytes);

    int gen_acc = 0, gen_n = 0, imp_acc = 0, imp_n = 0, max_imp = 0;
    for (int i = 0; i < 20; i++) {
        load_pgm(g.gl_pathv[i], px);
        ft_features *p = ft_extract(px); int best = 0;
        for (int k = 0; k < 40; k++) { int s = ft_match_pair(gal[k], p); if (s > best) best = s; }
        printf("  genuine %02d: %2d inliers  quality %d\n", i, best, ft_image_quality(px));
        gen_acc += best >= thr; gen_n++; ft_features_free(p);
    }
    globfree(&g);
    const char *imps[] = { "right-middle", "left-index", "right-thumb", "right-ring" };
    for (int d = 0; d < 4; d++) {
        snprintf(pat, sizeof pat, "%s/%s/*.pgm", argv[1], imps[d]);
        glob(pat, 0, NULL, &g);
        for (size_t i = 0; i < g.gl_pathc; i++) {
            load_pgm(g.gl_pathv[i], px);
            ft_features *p = ft_extract(px); int best = 0;
            for (int k = 0; k < 40; k++) { int s = ft_match_pair(gal[k], p); if (s > best) best = s; }
            if (best > max_imp) max_imp = best;
            imp_acc += best >= thr; imp_n++; ft_features_free(p);
        }
        globfree(&g);
    }
    printf("threshold %d: genuine %d/%d accepted, impostors %d/%d accepted (best impostor %d)\n",
           thr, gen_acc, gen_n, imp_acc, imp_n, max_imp);
    return 0;
}
