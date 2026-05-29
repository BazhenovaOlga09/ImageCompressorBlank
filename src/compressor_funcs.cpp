#include "compressor_funcs.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "colors.h"
#include "error_handlers.h"
#include "images.h"
#include "libbmp.h"

// ---- BMP ----

UncompressedImage loadFromBMP(const std::string& filename) {
    BMP bmp(filename.c_str());
    UncompressedImage img;
    img.width = static_cast<uint32_t>(bmp.get_width());
    img.height = static_cast<uint32_t>(bmp.get_height());
    img.is_grayscale = false;
    img.image_data.resize(img.height, std::vector<ColorRGB>(img.width));
    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            uint8_t r, g, b;
            bmp.get_pixel(static_cast<int>(j), static_cast<int>(i), r, g, b);
            img.image_data[i][j] = {r, g, b};
        }
    }
    return img;
}

void saveAsBMP(const UncompressedImage& img, const std::string& filename) {
    BMP bmp(static_cast<int>(img.width), static_cast<int>(img.height));
    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            const ColorRGB& c = img.image_data[i][j];
            bmp.set_pixel(static_cast<int>(j), static_cast<int>(i), c.r, c.g, c.b);
        }
    }
    bmp.write(filename.c_str());
}

// ---- RAW file format ----
// magic:   "RAWIMAGE"  = 8 bytes (no null terminator)
// end:     "RAWIMGEND" = 9 bytes (no null terminator)
// version: 1, 0, 0

static void writeBytes(std::fstream& f, const void* data, size_t n) {
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
}

static void readBytes(std::fstream& f, void* data, size_t n) {
    f.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
}

void writeUncompressedFile(const std::string& filename, const UncompressedImage& img) {
    std::fstream f(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        handleLogMessage("Cannot open file " + filename, Severity::CRITICAL, 1);
    }

    // 8-byte magic
    f.write("RAWIMAGE", 8);
    // 3-byte version
    uint8_t version[3] = {1, 0, 0};
    writeBytes(f, version, 3);
    // dimensions
    writeBytes(f, &img.width, 4);
    writeBytes(f, &img.height, 4);
    // grayscale flag
    uint8_t gs = img.is_grayscale ? 1 : 0;
    writeBytes(f, &gs, 1);

    if (img.is_grayscale) {
        for (uint32_t i = 0; i < img.height; ++i) {
            for (uint32_t j = 0; j < img.width; ++j) {
                uint8_t val = img.image_data[i][j].r;
                writeBytes(f, &val, 1);
            }
        }
    } else {
        for (uint32_t i = 0; i < img.height; ++i) {
            for (uint32_t j = 0; j < img.width; ++j) {
                writeBytes(f, &img.image_data[i][j].r, 1);
                writeBytes(f, &img.image_data[i][j].g, 1);
                writeBytes(f, &img.image_data[i][j].b, 1);
            }
        }
    }
    // 9-byte end signature
    f.write("RAWIMGEND", 9);
    f.close();
}

UncompressedImage readUncompressedFile(const std::string& filename) {
    std::fstream f(filename, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        handleLogMessage("Cannot open file " + filename, Severity::CRITICAL, 1);
    }

    char magic[8];
    readBytes(f, magic, 8);
    if (std::memcmp(magic, "RAWIMAGE", 8) != 0) {
        handleLogMessage("Invalid RAW magic in " + filename, Severity::CRITICAL, 1);
    }

    uint8_t version[3];
    readBytes(f, version, 3);

    UncompressedImage img;
    readBytes(f, &img.width, 4);
    readBytes(f, &img.height, 4);
    uint8_t gs;
    readBytes(f, &gs, 1);
    img.is_grayscale = (gs == 1);
    img.image_data.resize(img.height, std::vector<ColorRGB>(img.width));

    if (img.is_grayscale) {
        for (uint32_t i = 0; i < img.height; ++i) {
            for (uint32_t j = 0; j < img.width; ++j) {
                uint8_t val;
                readBytes(f, &val, 1);
                img.image_data[i][j] = {val, val, val};
            }
        }
    } else {
        for (uint32_t i = 0; i < img.height; ++i) {
            for (uint32_t j = 0; j < img.width; ++j) {
                readBytes(f, &img.image_data[i][j].r, 1);
                readBytes(f, &img.image_data[i][j].g, 1);
                readBytes(f, &img.image_data[i][j].b, 1);
            }
        }
    }

    char end[9];
    readBytes(f, end, 9);
    if (std::memcmp(end, "RAWIMGEND", 9) != 0) {
        handleLogMessage("Invalid RAW end signature in " + filename, Severity::WARNING);
    }
    f.close();
    return img;
}

// ---- Compression helpers ----

uint8_t findClosestColorId(
    const ColorRGB& color, const std::map<uint8_t, ColorRGB>& colorTable) {
    uint8_t best_id = colorTable.begin()->first;
    int64_t best_dist = INT64_MAX;
    for (const auto& [id, c] : colorTable) {
        int64_t d = colorDistanceSq(color, c);
        if (d < best_dist) {
            best_dist = d;
            best_id = id;
        }
    }
    return best_id;
}

CompressedImage toCompressed(
    const UncompressedImage& img,
    const std::map<uint8_t, ColorRGB>& color_table,
    bool approximate,
    bool allow_color_add) {
    CompressedImage result;
    result.width = img.width;
    result.height = img.height;
    result.id_to_color = color_table;

    result.color_to_id.clear();
    for (const auto& [id, c] : result.id_to_color) {
        result.color_to_id[c] = id;
    }

    result.image_data.resize(img.height, std::vector<uint8_t>(img.width));

    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            const ColorRGB& pixel = img.image_data[i][j];

            if (!approximate) {
                auto it = result.color_to_id.find(pixel);
                if (it != result.color_to_id.end()) {
                    result.image_data[i][j] = it->second;
                } else if (allow_color_add) {
                    uint8_t new_id = static_cast<uint8_t>(result.id_to_color.size());
                    result.id_to_color[new_id] = pixel;
                    result.color_to_id[pixel] = new_id;
                    result.image_data[i][j] = new_id;
                } else {
                    result.image_data[i][j] = findClosestColorId(pixel, result.id_to_color);
                }
            } else {
                if (result.id_to_color.empty()) {
                    if (allow_color_add) {
                        result.id_to_color[0] = pixel;
                        result.color_to_id[pixel] = 0;
                        result.image_data[i][j] = 0;
                    }
                } else {
                    result.image_data[i][j] = findClosestColorId(pixel, result.id_to_color);
                }
            }
        }
    }
    return result;
}

UncompressedImage toUncompressed(const CompressedImage& img) {
    UncompressedImage result;
    result.width = img.width;
    result.height = img.height;
    result.is_grayscale = false;
    result.image_data.resize(img.height, std::vector<ColorRGB>(img.width));
    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            uint8_t id = img.image_data[i][j];
            auto it = img.id_to_color.find(id);
            if (it != img.id_to_color.end()) {
                result.image_data[i][j] = it->second;
            }
        }
    }
    return result;
}

ColorRGB getColor(const CompressedImage& img, int x, int y) {
    uint8_t id = img.image_data[static_cast<size_t>(y)][static_cast<size_t>(x)];
    return img.id_to_color.at(id);
}

// ---- COMPRESSED file format ----
// magic:   "CMPRIMAGE" + 0x00 = 10 bytes
// end:     "CMPRIMGEND"       = 10 bytes
// version: 6, 6, 6

static uint8_t calcPow(size_t n) {
    if (n == 0) return 0;
    uint8_t p = 0;
    size_t s = 1;
    while (s < n) {
        ++p;
        s <<= 1;
    }
    return p;
}

void writeCompressedFile(const std::string& filename, const CompressedImage& img) {
    std::fstream f(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        handleLogMessage("Cannot open file " + filename, Severity::CRITICAL, 1);
    }

    // 10-byte magic: "CMPRIMAGE\0"
    f.write("CMPRIMAGE", 9);
    uint8_t zero = 0;
    writeBytes(f, &zero, 1);

    // 3-byte version
    uint8_t version[3] = {6, 6, 6};
    writeBytes(f, version, 3);

    writeBytes(f, &img.width, 4);
    writeBytes(f, &img.height, 4);

    size_t palette_size = img.id_to_color.size();
    uint8_t pow = calcPow(palette_size);
    size_t table_size = static_cast<size_t>(1) << pow;
    writeBytes(f, &pow, 1);

    for (size_t idx = 0; idx < table_size; ++idx) {
        auto it = img.id_to_color.find(static_cast<uint8_t>(idx));
        if (it != img.id_to_color.end()) {
            writeBytes(f, &it->second.r, 1);
            writeBytes(f, &it->second.g, 1);
            writeBytes(f, &it->second.b, 1);
        } else {
            uint8_t z = 0;
            writeBytes(f, &z, 1);
            writeBytes(f, &z, 1);
            writeBytes(f, &z, 1);
        }
    }

    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            writeBytes(f, &img.image_data[i][j], 1);
        }
    }

    // 10-byte end signature
    f.write("CMPRIMGEND", 10);
    f.close();
}

CompressedImage readCompressedFile(const std::string& filename) {
    std::fstream f(filename, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        handleLogMessage("Cannot open file " + filename, Severity::CRITICAL, 1);
    }

    char magic[10];
    readBytes(f, magic, 10);
    if (std::memcmp(magic, "CMPRIMAGE\0", 10) != 0) {
        handleLogMessage("Invalid CMPR magic in " + filename, Severity::CRITICAL, 1);
    }

    uint8_t version[3];
    readBytes(f, version, 3);

    CompressedImage img;
    readBytes(f, &img.width, 4);
    readBytes(f, &img.height, 4);

    uint8_t pow;
    readBytes(f, &pow, 1);
    size_t table_size = static_cast<size_t>(1) << pow;

    for (size_t idx = 0; idx < table_size; ++idx) {
        ColorRGB c;
        readBytes(f, &c.r, 1);
        readBytes(f, &c.g, 1);
        readBytes(f, &c.b, 1);
        img.id_to_color[static_cast<uint8_t>(idx)] = c;
        img.color_to_id[c] = static_cast<uint8_t>(idx);
    }

    img.image_data.resize(img.height, std::vector<uint8_t>(img.width));
    for (uint32_t i = 0; i < img.height; ++i) {
        for (uint32_t j = 0; j < img.width; ++j) {
            readBytes(f, &img.image_data[i][j], 1);
        }
    }

    char end[10];
    readBytes(f, end, 10);
    if (std::memcmp(end, "CMPRIMGEND", 10) != 0) {
        handleLogMessage("Invalid CMPR end signature in " + filename, Severity::WARNING);
    }
    f.close();
    return img;
}
