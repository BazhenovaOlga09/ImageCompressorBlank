#include "colors.h"

#include <cstdint>
#include <fstream>

int64_t colorDistanceSq(const ColorRGB& color1, const ColorRGB& color2) {
    int64_t dr = static_cast<int64_t>(color1.r) - static_cast<int64_t>(color2.r);
    int64_t dg = static_cast<int64_t>(color1.g) - static_cast<int64_t>(color2.g);
    int64_t db = static_cast<int64_t>(color1.b) - static_cast<int64_t>(color2.b);
    return dr * dr + dg * dg + db * db;
}

uint8_t colorToGrayscale(const ColorRGB& color) {
    return static_cast<uint8_t>(
        (static_cast<int>(color.r) + static_cast<int>(color.g) + static_cast<int>(color.b)) / 3);
}

ColorRGB readFromFileStream(std::fstream& stream) {
    ColorRGB color;
    stream.read(reinterpret_cast<char*>(&color.r), 1);
    stream.read(reinterpret_cast<char*>(&color.g), 1);
    stream.read(reinterpret_cast<char*>(&color.b), 1);
    return color;
}
