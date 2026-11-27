// SPDX-License-Identifier: LGPL-2.1-or-later
// Local benchmark. Never sends biometric data anywhere.
#include "ftmatch.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Feature = std::unique_ptr<ft_features, decltype(&ft_features_free)>;
struct Touch { Feature features{nullptr, ft_features_free}; int quality; };

static std::vector<Touch> load(const std::string &root, const char *finger, int count)
{
    std::vector<Touch> result;
    for (int i = 0; i < count; ++i) {
        char suffix[128];
        snprintf(suffix, sizeof suffix, "/%s/%02d.pgm", finger, i);
        cv::Mat image = cv::imread(root + suffix, cv::IMREAD_UNCHANGED);
        if (image.type() != CV_16UC1 || image.cols != FT_IMG_W || image.rows != FT_IMG_H)
            throw std::runtime_error("missing or invalid image: " + root + suffix);
        auto *px = image.ptr<uint16_t>();
        Touch t;
        t.quality = ft_image_quality(px);
        Feature extracted(ft_extract(px), ft_features_free);
        // Exercise the actual descriptor quantisation used by stored prints.
        uint8_t *blob = nullptr;
        size_t size = ft_serialize(extracted.get(), &blob);
        t.features.reset(ft_deserialize(blob, size, nullptr));
        free(blob);
        if (!t.features) throw std::runtime_error("template roundtrip failed");
        result.push_back(std::move(t));
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s dataset-directory [--export-gallery|--load-gallery directory]\n", argv[0]); return 1;
    }
    try {
        auto genuine = load(argv[1], "right-index", 60);
        auto middle = load(argv[1], "right-middle", 20);
        auto left = load(argv[1], "left-index", 20);
        if (argc == 4) {
            std::string mode = argv[2];
            if (mode != "--export-gallery" && mode != "--load-gallery")
                throw std::runtime_error("invalid gallery option");
            if (mode == "--export-gallery") std::filesystem::create_directories(argv[3]);
            for (int i = 0; i < 40; ++i) {
                std::string path = std::string(argv[3]) + "/" + std::to_string(i) + ".bin";
                if (mode == "--export-gallery") {
                    uint8_t *blob = nullptr;
                    size_t size = ft_serialize(genuine[i].features.get(), &blob);
                    std::ofstream out(path, std::ios::binary);
                    out.write(reinterpret_cast<char *>(blob), size);
                    free(blob);
                    if (!out) throw std::runtime_error("cannot write gallery");
                } else {
                    std::ifstream in(path, std::ios::binary);
                    if (!in) throw std::runtime_error("cannot read gallery");
                    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), {});
                    genuine[i].features.reset(ft_deserialize(blob.data(), blob.size(), nullptr));
                    if (!genuine[i].features) throw std::runtime_error("invalid gallery");
                }
            }
        }
        puts("finger,index,split,quality,features,score,reverse_score,match_ms");
        auto score = [&](const char *finger, std::vector<Touch> &touches, int start) {
            for (int i = start; i < (int)touches.size(); ++i) {
                auto begin = std::chrono::steady_clock::now();
                auto &t = touches[i];
                int best = 0, reverse = 0;
                for (int k = 0; k < 40; ++k) {
                    best = std::max(best, ft_match_pair(genuine[k].features.get(), t.features.get()));
                    reverse = std::max(reverse, ft_match_pair(t.features.get(), genuine[k].features.get()));
                }
                double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                const char *split = (start ? i < 50 : i < 10) ? "development" : "holdout";
                printf("%s,%d,%s,%d,%d,%d,%d,%.2f\n", finger, i, split, t.quality,
                       ft_feature_count(t.features.get()), best, reverse, ms);
                fflush(stdout);
            }
        };
        score("right-index", genuine, 40);
        score("right-middle", middle, 0);
        score("left-index", left, 0);
    } catch (const std::exception &e) { fprintf(stderr, "%s\n", e.what()); return 1; }
}
