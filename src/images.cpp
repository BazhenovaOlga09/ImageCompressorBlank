#include "images.h"

#include <iostream>

bool matchUncompressedImages(
    const UncompressedImage& img1, const UncompressedImage& img2, bool verbose) {
    if (img1.width != img2.width || img1.height != img2.height) {
        if (verbose) {
            std::cout << "Size mismatch: " << img1.width << "x" << img1.height << " vs "
                      << img2.width << "x" << img2.height << std::endl;
        }
        return false;
    }
    if (img1.is_grayscale != img2.is_grayscale) {
        if (verbose) {
            std::cout << "Grayscale mismatch" << std::endl;
        }
        return false;
    }
    for (uint32_t i = 0; i < img1.height; ++i) {
        for (uint32_t j = 0; j < img1.width; ++j) {
            if (img1.image_data[i][j] != img2.image_data[i][j]) {
                if (verbose) {
                    std::cout << "Pixel mismatch at (" << i << ", " << j << ")" << std::endl;
                }
                return false;
            }
        }
    }
    return true;
}
