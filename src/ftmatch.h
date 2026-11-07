// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * ftmatch - open-source fingerprint matcher for small (64x80) capacitive
 * sensors such as the FocalTech 2808:c652.
 *
 * Each touch is reduced to SIFT keypoints (position + 128-byte descriptor).
 * A template is a gallery of touches; a probe matches when RANSAC finds a
 * rigid alignment to any gallery touch with at least `threshold` inliers.
 * No images are stored, only features.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_IMG_W 64
#define FT_IMG_H 80

typedef struct ft_features ft_features;   /* features of one touch */

/* Image quality gate. Returns 0 if usable, otherwise a reason code below. */
enum { FT_Q_OK = 0, FT_Q_PARTIAL = 1, FT_Q_LOW_CONTRAST = 2, FT_Q_FEW_FEATURES = 3 };
int ft_image_quality(const uint16_t *px);

/* Extract features from a raw sensor image (FT_IMG_W*FT_IMG_H, row-major). */
ft_features *ft_extract(const uint16_t *px);
int ft_feature_count(const ft_features *f);
void ft_features_free(ft_features *f);

/* Best RANSAC inlier count of probe against one gallery touch. */
int ft_match_pair(const ft_features *gallery_touch, const ft_features *probe);

/* Serialization (portable little-endian byte blob). */
size_t ft_serialize(const ft_features *f, uint8_t **out);      /* caller frees with free() */
ft_features *ft_deserialize(const uint8_t *buf, size_t len, size_t *consumed);

#ifdef __cplusplus
}
#endif
