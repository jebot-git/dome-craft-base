// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
namespace artcraft {
std::vector<unsigned char> decode_base64(const std::string &value, size_t limit=32*1024*1024);
std::pair<int,int> png_dimensions(const std::vector<unsigned char> &bytes);
std::string encode_base64(const std::vector<unsigned char> &value);
// Rasterize one page of the bitmap PDF in a private temporary directory, without a shell.
std::string bitmap_pdf_png(const std::string &pdf, int page);
}
