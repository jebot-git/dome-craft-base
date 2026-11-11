// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
namespace dome_office {
struct PageRect {
  double x = 0, y = 0, w = 0, h = 0;
  int page = 0;
};
struct WordLayout {
  std::vector<PageRect> pages;
  double scale = 4. / 3., width = 0, height = 0;
  static WordLayout place(const std::vector<PageRect> &sizes, double width, double height,
                          double zoom, const std::string &fit, bool single = false,
                          bool multiple = false) {
    WordLayout out;
    const double gap = 20.;
    width = std::max(80., width);
    height = std::max(80., height);
    double maxw = 72, maxh = 72;
    for (auto p : sizes) {
      maxw = std::max(maxw, p.w);
      maxh = std::max(maxh, p.h);
    }
    out.scale = std::clamp(zoom, .1, 5.) * 4. / 3.;
    if (fit == "pageWidth")
      out.scale = std::clamp((width - 2 * gap) / maxw, .05, 6.);
    if (fit == "onePage")
      out.scale =
          std::clamp(std::min((width - 2 * gap) / maxw, (height - 2 * gap) / maxh), .05, 6.);
    if (fit == "multiplePages")
      out.scale =
          std::clamp(std::min((width - 3 * gap) / (2 * maxw), (height - 2 * gap) / maxh), .05, 6.);
    int cols = single || fit == "pageWidth" || fit == "onePage"
                   ? 1
                   : std::max(1, int((width - gap) / (maxw * out.scale + gap)));
    if (multiple || fit == "multiplePages")
      cols = std::max(2, cols);
    cols = std::min(cols, std::max(1, int(sizes.size())));
    const double roww = cols * maxw * out.scale + (cols - 1) * gap;
    out.width = std::max(width, roww + 2 * gap);
    double y = gap;
    for (size_t first = 0; first < sizes.size(); first += cols) {
      double rowh = 0;
      for (int col = 0; col < cols && first + col < sizes.size(); ++col) {
        auto p = sizes[first + col];
        rowh = std::max(rowh, p.h * out.scale);
        out.pages.push_back(
            {(out.width - roww) / 2 + col * (maxw * out.scale + gap) + (maxw - p.w) * out.scale / 2,
             y, p.w * out.scale, p.h * out.scale, p.page});
      }
      y += rowh + gap;
    }
    out.height = std::max(height, y);
    return out;
  }
  const PageRect *hit(double x, double y) const {
    for (const auto &p : pages)
      if (x >= p.x && x <= p.x + p.w && y >= p.y && y <= p.y + p.h)
        return &p;
    return nullptr;
  }
};
} // namespace dome_office
