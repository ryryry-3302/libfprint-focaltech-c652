// ftmatch.cpp - see ftmatch.h. Port of bench/match.py (prep + sift_score).
#include "ftmatch.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

struct ft_features {
    std::vector<cv::Point2f> pts;
    cv::Mat desc;   // N x 128, CV_32F (stored as uint8 when serialized)
};

namespace {

cv::Mat raw_image(const uint16_t *px)
{
    cv::Mat m(FT_IMG_H, FT_IMG_W, CV_32F);
    for (int i = 0; i < FT_IMG_W * FT_IMG_H; i++) m.at<float>(i / FT_IMG_W, i % FT_IMG_W) = px[i];
    return m;
}

float median_of(std::vector<float> v)
{
    // numpy-compatible median (mean of the two middle values for even sizes)
    size_t n = v.size();
    std::sort(v.begin(), v.end());
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

// Normalised ridge image and finger mask (2x upscaled, borders cropped).
void prep(const uint16_t *px, cv::Mat &norm, cv::Mat &mask)
{
    cv::Mat raw0 = raw_image(px);
    cv::Mat im = raw0(cv::Rect(2, 2, FT_IMG_W - 4, FT_IMG_H - 4)).clone();
    for (int x = 0; x < im.cols; x++) {                    // per-column fixed offset
        std::vector<float> c(im.rows);
        for (int y = 0; y < im.rows; y++) c[y] = im.at<float>(y, x);
        float m = median_of(c);
        for (int y = 0; y < im.rows; y++) im.at<float>(y, x) -= m;
    }
    for (int y = 0; y < im.rows; y++) {                    // per-row fixed offset
        std::vector<float> r(im.cols);
        for (int x = 0; x < im.cols; x++) r[x] = im.at<float>(y, x);
        float m = median_of(r);
        for (int x = 0; x < im.cols; x++) im.at<float>(y, x) -= m;
    }
    cv::Mat raw, up, bg, sd;
    cv::resize(raw0(cv::Rect(2, 2, FT_IMG_W - 4, FT_IMG_H - 4)), raw, cv::Size(), 2, 2, cv::INTER_CUBIC);
    cv::resize(im, up, cv::Size(), 2, 2, cv::INTER_CUBIC);
    cv::GaussianBlur(up, bg, cv::Size(0, 0), 8);
    cv::Mat hp = up - bg;
    cv::GaussianBlur(hp.mul(hp), sd, cv::Size(0, 0), 8);
    cv::sqrt(sd, sd);
    sd += 1e-3;
    cv::Mat n = hp / sd;

    cv::Mat sm;
    cv::GaussianBlur(raw, sm, cv::Size(0, 0), 6);
    std::vector<float> v(sm.begin<float>(), sm.end<float>());
    std::sort(v.begin(), v.end());
    // numpy percentile (linear interpolation)
    double pos = 0.95 * (v.size() - 1);
    size_t lo = (size_t)pos;
    double p95 = v[lo] + (pos - lo) * (v[std::min(lo + 1, v.size() - 1)] - v[lo]);
    mask = sm > 0.5 * p95;                                   // CV_8U 0/255
    cv::erode(mask, mask, cv::Mat::ones(5, 5, CV_8U));
    cv::Mat m01; mask.convertTo(m01, CV_32F, 1.0 / 255);
    norm = n.mul(m01);
}

cv::Ptr<cv::SIFT> sift()
{
    static cv::Ptr<cv::SIFT> s = cv::SIFT::create(0, 3, 0.02);
    return s;
}

}  // namespace

extern "C" {

int ft_image_quality(const uint16_t *px)
{
    cv::Mat norm, mask;
    prep(px, norm, mask);
    double coverage = cv::countNonZero(mask) / (double)mask.total();
    if (coverage < 0.35) return FT_Q_PARTIAL;
    cv::Mat raw = raw_image(px);
    cv::Scalar mean, sd;
    cv::meanStdDev(raw, mean, sd);
    if (sd[0] < 30) return FT_Q_LOW_CONTRAST;
    return FT_Q_OK;
}

ft_features *ft_extract(const uint16_t *px)
{
    cv::Mat norm, mask, u8;
    prep(px, norm, mask);
    cv::normalize(norm, u8, 0, 255, cv::NORM_MINMAX);
    u8.convertTo(u8, CV_8U);   // truncation, matching numpy astype(uint8)
    cv::Mat m8 = mask > 127;
    std::vector<cv::KeyPoint> kp;
    auto *f = new ft_features;
    sift()->detectAndCompute(u8, m8, kp, f->desc);
    for (auto &k : kp) f->pts.push_back(k.pt);
    return f;
}

int ft_feature_count(const ft_features *f) { return f ? (int)f->pts.size() : 0; }
void ft_features_free(ft_features *f) { delete f; }

int ft_match_pair(const ft_features *a, const ft_features *b)
{
    if (!a || !b || a->pts.size() < 3 || b->pts.size() < 3) return 0;
    static cv::BFMatcher bf(cv::NORM_L2);
    std::vector<std::vector<cv::DMatch>> knn;
    bf.knnMatch(a->desc, b->desc, knn, 2);
    std::vector<cv::Point2f> src, dst;
    for (auto &p : knn)
        if (p.size() == 2 && p[0].distance < 0.8f * p[1].distance) {
            src.push_back(a->pts[p[0].queryIdx]);
            dst.push_back(b->pts[p[0].trainIdx]);
        }
    if (src.size() < 3) return 0;
    std::vector<uchar> inl;
    cv::setRNGSeed(0x2808);   // deterministic RANSAC: same inputs, same score
    cv::Mat M = cv::estimateAffinePartial2D(src, dst, inl, cv::RANSAC, 4, 4000, 0.995);
    if (M.empty()) return 0;
    double sc = std::hypot(M.at<double>(0, 0), M.at<double>(1, 0));
    if (!(sc > 0.85 && sc < 1.15)) return 0;
    return cv::countNonZero(inl);
}

// blob: u32 count, then count * (f32 x, f32 y, u8 desc[128])
size_t ft_serialize(const ft_features *f, uint8_t **out)
{
    uint32_t n = (uint32_t)f->pts.size();
    size_t len = 4 + (size_t)n * (8 + 128);
    uint8_t *b = (uint8_t *)malloc(len), *p = b + 4;
    memcpy(b, &n, 4);
    for (uint32_t i = 0; i < n; i++) {
        memcpy(p, &f->pts[i].x, 4); memcpy(p + 4, &f->pts[i].y, 4); p += 8;
        for (int j = 0; j < 128; j++) *p++ = cv::saturate_cast<uint8_t>(f->desc.at<float>(i, j));
    }
    *out = b;
    return len;
}

ft_features *ft_deserialize(const uint8_t *buf, size_t len, size_t *consumed)
{
    if (len < 4) return nullptr;
    uint32_t n; memcpy(&n, buf, 4);
    size_t need = 4 + (size_t)n * (8 + 128);
    if (len < need) return nullptr;
    auto *f = new ft_features;
    f->desc.create(n, 128, CV_32F);
    const uint8_t *p = buf + 4;
    for (uint32_t i = 0; i < n; i++) {
        cv::Point2f pt; memcpy(&pt.x, p, 4); memcpy(&pt.y, p + 4, 4); p += 8;
        f->pts.push_back(pt);
        for (int j = 0; j < 128; j++) f->desc.at<float>(i, j) = *p++;
    }
    if (consumed) *consumed = need;
    return f;
}

}  // extern "C"
