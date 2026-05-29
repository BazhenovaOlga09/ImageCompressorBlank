#include "libbmp.h"
#include <iostream>
#include <fstream>
#include <vector>

BMP::BMP(int width, int height, bool has_alpha) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("The image width and height must be positive numbers.");
    }

    bmp_info_header.width = width;
    bmp_info_header.height = height;
    bmp_info_header.bit_count = has_alpha ? 32 : 24;
    bmp_info_header.size = sizeof(BMPInfoHeader) + (has_alpha ? sizeof(BMPColorHeader) : 0);
    file_header.offset_data = sizeof(BMPHeader) + bmp_info_header.size;
    file_header.file_size = file_header.offset_data;

    row_stride = width * bmp_info_header.bit_count / 8;
    data.resize(row_stride * height);

    if (has_alpha) {
        bmp_color_header.red_mask = 0x00ff0000;
        bmp_color_header.green_mask = 0x0000ff00;
        bmp_color_header.blue_mask = 0x000000ff;
        bmp_color_header.alpha_mask = 0xff000000;
        bmp_color_header.color_space_type = 0x73524742;  // sRGB
    }
}

BMP::BMP(const char* fname) { read(fname); }

void BMP::read(const char* fname) {
    std::ifstream inp{fname, std::ios_base::binary};
    if (inp) {
        inp.read((char*)&file_header, sizeof(file_header));
        if (file_header.file_type != 0x4D42) {
            throw std::runtime_error("Error! Unrecognized file format.");
        }
        inp.read((char*)&bmp_info_header, sizeof(bmp_info_header));

        if (bmp_info_header.bit_count == 32) {
            if (bmp_info_header.size >= (sizeof(BMPInfoHeader) + sizeof(BMPColorHeader))) {
                inp.read((char*)&bmp_color_header, sizeof(bmp_color_header));
            } else {
                std::cerr << "Error! The file \"" << fname
                          << "\" does not seem to contain bit mask information\n";
                throw std::runtime_error("Error! Unrecognized file format.");
            }
        }

        inp.seekg(file_header.offset_data, inp.beg);

        if (bmp_info_header.bit_count == 32) {
            bmp_info_header.size = sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
            file_header.offset_data =
                sizeof(BMPHeader) + sizeof(BMPInfoHeader) + sizeof(BMPColorHeader);
        } else {
            bmp_info_header.size = sizeof(BMPInfoHeader);
            file_header.offset_data = sizeof(BMPHeader) + sizeof(BMPInfoHeader);
        }
        file_header.file_size = file_header.offset_data;

        if (bmp_info_header.height < 0) {
            throw std::runtime_error(
                "The program can treat only BMP images with the origin in the bottom left corner!");
        }

        data.resize(bmp_info_header.width * bmp_info_header.height * bmp_info_header.bit_count / 8);

        if (bmp_info_header.width % 4 == 0) {
            inp.read((char*)data.data(), data.size());
            file_header.file_size += data.size();
        } else {
            row_stride = bmp_info_header.width * bmp_info_header.bit_count / 8;
            uint32_t new_stride = make_stride_aligned(4);
            std::vector<uint8_t> padding_row(new_stride - row_stride);

            for (int y = 0; y < bmp_info_header.height; ++y) {
                inp.read((char*)(data.data() + row_stride * y), row_stride);
                inp.read((char*)padding_row.data(), padding_row.size());
            }
            file_header.file_size += data.size() + bmp_info_header.height * padding_row.size();
        }
    } else {
        throw std::runtime_error("Unable to open the input image file.");
    }
}

void BMP::write(const char* fname) {
    std::ofstream of{fname, std::ios_base::binary};
    if (of) {
        if (bmp_info_header.bit_count == 32) {
            write_headers_and_data(of);
        } else if (bmp_info_header.bit_count == 24) {
            if (bmp_info_header.width % 4 == 0) {
                write_headers_and_data(of);
            } else {
                uint32_t new_stride = make_stride_aligned(4);
                std::vector<uint8_t> padding_row(new_stride - row_stride);

                write_headers(of);

                for (int y = 0; y < bmp_info_header.height; ++y) {
                    of.write((const char*)(data.data() + row_stride * y), row_stride);
                    of.write((const char*)padding_row.data(), padding_row.size());
                }
            }
        } else {
            throw std::runtime_error(
                "The program can treat only 24 or 32 bits per pixel BMP files");
        }
    } else {
        throw std::runtime_error("Unable to open the output image file.");
    }
}

void BMP::write_headers(std::ofstream& of) {
    of.write((const char*)&file_header, sizeof(file_header));
    of.write((const char*)&bmp_info_header, sizeof(bmp_info_header));
    if (bmp_info_header.bit_count == 32) {
        of.write((const char*)&bmp_color_header, sizeof(bmp_color_header));
    }
}

void BMP::write_headers_and_data(std::ofstream& of) {
    write_headers(of);
    of.write((const char*)data.data(), data.size());
}

uint32_t BMP::make_stride_aligned(uint32_t align_stride) {
    uint32_t new_stride = row_stride;
    while (new_stride % align_stride != 0) {
        new_stride++;
    }
    return new_stride;
}

void BMP::set_pixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t channels = bmp_info_header.bit_count / 8;
    uint32_t index = (y * bmp_info_header.width + x) * channels;
    data[index] = b;
    data[index + 1] = g;
    data[index + 2] = r;
}

void BMP::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t channels = bmp_info_header.bit_count / 8;
    uint32_t index = (y * bmp_info_header.width + x) * channels;
    data[index] = b;
    data[index + 1] = g;
    data[index + 2] = r;
    if (channels == 4) {
        data[index + 3] = a;
    }
}

void BMP::get_pixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b) const {
    uint32_t channels = bmp_info_header.bit_count / 8;
    uint32_t index = (y * bmp_info_header.width + x) * channels;
    b = data[index];
    g = data[index + 1];
    r = data[index + 2];
}

void BMP::get_pixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const {
    uint32_t channels = bmp_info_header.bit_count / 8;
    uint32_t index = (y * bmp_info_header.width + x) * channels;
    b = data[index];
    g = data[index + 1];
    r = data[index + 2];
    if (channels == 4) {
        a = data[index + 3];
    }
}

int BMP::get_width() const { return bmp_info_header.width; }

int BMP::get_height() const { return bmp_info_header.height; }
