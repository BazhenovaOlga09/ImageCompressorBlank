#include "image_transforms.h"

#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <vector>

#include "colors.h"
#include "compressor_funcs.h"
#include "images.h"

// ---- mirror ----

template <>
void mirror<UncompressedImage>(UncompressedImage& img, bool horizontal) {
    if (horizontal) {
        for (auto& row : img.image_data) {
            std::reverse(row.begin(), row.end());
        }
    } else {
        std::reverse(img.image_data.begin(), img.image_data.end());
    }
}

template <>
void mirror<CompressedImage>(CompressedImage& img, bool horizontal) {
    if (horizontal) {
        for (auto& row : img.image_data) {
            std::reverse(row.begin(), row.end());
        }
    } else {
        std::reverse(img.image_data.begin(), img.image_data.end());
    }
}

// ---- negative ----

void negative(UncompressedImage& img) {
    for (auto& row : img.image_data) {
        for (auto& pixel : row) {
            pixel.r = static_cast<uint8_t>(255 - pixel.r);
            pixel.g = static_cast<uint8_t>(255 - pixel.g);
            pixel.b = static_cast<uint8_t>(255 - pixel.b);
        }
    }
}

void negative(CompressedImage& img) {
    std::map<uint8_t, ColorRGB> new_id_to_color;
    for (auto& [id, c] : img.id_to_color) {
        new_id_to_color[id] = {
            static_cast<uint8_t>(255 - c.r),
            static_cast<uint8_t>(255 - c.g),
            static_cast<uint8_t>(255 - c.b)};
    }
    img.id_to_color = new_id_to_color;
    img.color_to_id.clear();
    for (auto& [id, c] : img.id_to_color) {
        img.color_to_id[c] = id;
    }
}

// ---- toGrayscale ----

void toGrayscale(UncompressedImage& img) {
    img.is_grayscale = true;
    for (auto& row : img.image_data) {
        for (auto& pixel : row) {
            uint8_t g = colorToGrayscale(pixel);
            pixel = {g, g, g};
        }
    }
}

void toGrayscale(CompressedImage& img) {
    std::map<uint8_t, ColorRGB> new_id_to_color;
    for (auto& [id, c] : img.id_to_color) {
        uint8_t g = colorToGrayscale(c);
        new_id_to_color[id] = {g, g, g};
    }
    img.id_to_color = new_id_to_color;
    img.color_to_id.clear();
    for (auto& [id, c] : img.id_to_color) {
        img.color_to_id[c] = id;
    }
}

// ---- rotate ----

void rotate(
    UncompressedImage& img, int angle, ColorRGB fill_color, bool smart_gap_interpolation) {
    angle = ((angle % 360) + 360) % 360;
    if (angle == 0) return;

    uint32_t W = img.width;
    uint32_t H = img.height;

    // Handle exact 90/180/270 without float math
    if (angle == 90) {
        // CCW 90: new[i][j] = old[j][W-1-i]
        std::vector<std::vector<ColorRGB>> new_data(W, std::vector<ColorRGB>(H));
        for (uint32_t i = 0; i < W; ++i)
            for (uint32_t j = 0; j < H; ++j)
                new_data[i][j] = img.image_data[j][W - 1 - i];
        img.width = H;
        img.height = W;
        img.image_data = std::move(new_data);
        return;
    }
    if (angle == 180) {
        std::vector<std::vector<ColorRGB>> new_data(H, std::vector<ColorRGB>(W));
        for (uint32_t i = 0; i < H; ++i)
            for (uint32_t j = 0; j < W; ++j)
                new_data[i][j] = img.image_data[H - 1 - i][W - 1 - j];
        img.image_data = std::move(new_data);
        return;
    }
    if (angle == 270) {
        // CCW 270 = CW 90: new[i][j] = old[H-1-j][i]
        std::vector<std::vector<ColorRGB>> new_data(W, std::vector<ColorRGB>(H));
        for (uint32_t i = 0; i < W; ++i)
            for (uint32_t j = 0; j < H; ++j)
                new_data[i][j] = img.image_data[H - 1 - j][i];
        img.width = H;
        img.height = W;
        img.image_data = std::move(new_data);
        return;
    }

    // Arbitrary angle — inverse mapping
    double rad = angle * M_PI / 180.0;
    double cos_a = std::cos(rad);
    double sin_a = std::sin(rad);

    double cx = W / 2.0;
    double cy = H / 2.0;

    double corners[4][2] = {
        {-cx, -cy}, {W - cx, -cy}, {W - cx, H - cy}, {-cx, H - cy}};

    double min_x = 1e18, max_x = -1e18, min_y = 1e18, max_y = -1e18;
    for (auto& c : corners) {
        double rx = c[0] * cos_a - c[1] * sin_a;
        double ry = c[0] * sin_a + c[1] * cos_a;
        min_x = std::min(min_x, rx);
        max_x = std::max(max_x, rx);
        min_y = std::min(min_y, ry);
        max_y = std::max(max_y, ry);
    }

    uint32_t new_W = static_cast<uint32_t>(std::ceil(max_x - min_x));
    uint32_t new_H = static_cast<uint32_t>(std::ceil(max_y - min_y));

    double new_cx = new_W / 2.0;
    double new_cy = new_H / 2.0;

    std::vector<std::vector<ColorRGB>> new_data(new_H, std::vector<ColorRGB>(new_W, fill_color));

    for (uint32_t ni = 0; ni < new_H; ++ni) {
        for (uint32_t nj = 0; nj < new_W; ++nj) {
            double dx = static_cast<double>(nj) - new_cx;
            double dy = static_cast<double>(ni) - new_cy;
            // inverse rotation (clockwise by angle = CCW by -angle)
            double ox = dx * cos_a + dy * sin_a + cx;
            double oy = -dx * sin_a + dy * cos_a + cy;

            int oi = static_cast<int>(std::round(oy));
            int oj = static_cast<int>(std::round(ox));

            if (oi >= 0 && oi < static_cast<int>(H) && oj >= 0 && oj < static_cast<int>(W)) {
                new_data[ni][nj] =
                    img.image_data[static_cast<uint32_t>(oi)][static_cast<uint32_t>(oj)];
            }
        }
    }

    if (smart_gap_interpolation) {
        for (int pass = 0; pass < 3; ++pass) {
            for (uint32_t ni = 0; ni < new_H; ++ni) {
                for (uint32_t nj = 0; nj < new_W; ++nj) {
                    if (new_data[ni][nj] == fill_color) {
                        int sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
                        const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                        for (auto& d : dirs) {
                            int ni2 = static_cast<int>(ni) + d[0];
                            int nj2 = static_cast<int>(nj) + d[1];
                            if (ni2 >= 0 && ni2 < static_cast<int>(new_H) && nj2 >= 0 &&
                                nj2 < static_cast<int>(new_W)) {
                                if (!(new_data[ni2][nj2] == fill_color)) {
                                    sum_r += new_data[ni2][nj2].r;
                                    sum_g += new_data[ni2][nj2].g;
                                    sum_b += new_data[ni2][nj2].b;
                                    ++count;
                                }
                            }
                        }
                        if (count > 0) {
                            new_data[ni][nj] = {
                                static_cast<uint8_t>(sum_r / count),
                                static_cast<uint8_t>(sum_g / count),
                                static_cast<uint8_t>(sum_b / count)};
                        }
                    }
                }
            }
        }
    }

    img.width = new_W;
    img.height = new_H;
    img.image_data = std::move(new_data);
}

// ---- kernel filters ----

void applyKernel(
    UncompressedImage& img, const std::vector<std::vector<int>>& kernel, int divisor) {
    int kh = static_cast<int>(kernel.size());
    int kw = static_cast<int>(kernel[0].size());
    int kcy = kh / 2;
    int kcx = kw / 2;

    uint32_t H = img.height;
    uint32_t W = img.width;

    std::vector<std::vector<ColorRGB>> result(H, std::vector<ColorRGB>(W, {0, 0, 0}));

    for (uint32_t i = 0; i < H; ++i) {
        for (uint32_t j = 0; j < W; ++j) {
            int sum_r = 0, sum_g = 0, sum_b = 0;
            for (int ki = 0; ki < kh; ++ki) {
                for (int kj = 0; kj < kw; ++kj) {
                    int si = std::clamp(static_cast<int>(i) + ki - kcy, 0, static_cast<int>(H) - 1);
                    int sj = std::clamp(static_cast<int>(j) + kj - kcx, 0, static_cast<int>(W) - 1);
                    int k = kernel[static_cast<size_t>(ki)][static_cast<size_t>(kj)];
                    sum_r += k * img.image_data[static_cast<uint32_t>(si)][static_cast<uint32_t>(sj)].r;
                    sum_g += k * img.image_data[static_cast<uint32_t>(si)][static_cast<uint32_t>(sj)].g;
                    sum_b += k * img.image_data[static_cast<uint32_t>(si)][static_cast<uint32_t>(sj)].b;
                }
            }
            if (divisor != 1) {
                sum_r /= divisor;
                sum_g /= divisor;
                sum_b /= divisor;
            }
            result[i][j] = {
                static_cast<uint8_t>(std::clamp(sum_r, 0, 255)),
                static_cast<uint8_t>(std::clamp(sum_g, 0, 255)),
                static_cast<uint8_t>(std::clamp(sum_b, 0, 255))};
        }
    }
    img.image_data = std::move(result);
}

void sharpen(UncompressedImage& img) {
    const std::vector<std::vector<int>> kernel = {{0, -1, 0}, {-1, 5, -1}, {0, -1, 0}};
    applyKernel(img, kernel, 1);
}

void gaussianBlurApprox(UncompressedImage& img, bool hard_blur) {
    if (!hard_blur) {
        const std::vector<std::vector<int>> kernel = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
        applyKernel(img, kernel, 16);
    } else {
        const std::vector<std::vector<int>> kernel = {
            {1, 1, 1, 1, 1},
            {1, 1, 1, 1, 1},
            {1, 1, 1, 1, 1},
            {1, 1, 1, 1, 1},
            {1, 1, 1, 1, 1}};
        applyKernel(img, kernel, 25);
    }
}

void edgeDetect(UncompressedImage& img) {
    const std::vector<std::vector<int>> kernel = {{0, 1, 0}, {1, -4, 1}, {0, 1, 0}};
    applyKernel(img, kernel, 1);
}