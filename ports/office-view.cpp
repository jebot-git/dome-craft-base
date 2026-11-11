// SPDX-License-Identifier: MIT
#include "office-view.hpp"
#include "office-ui.hpp"
#include "word-layout.hpp"
#include "gpu-image.hpp"
#include <artcraft/raster.hpp>
#include <artcraft/application.hpp>
#include <cctype>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <memory>
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
namespace dome_office {
namespace {
std::string text(const Json &v) {
  return v.is_null() ? "" : v.is_string() ? v.get<std::string>() : v.dump();
}
Json value_rows(const Json &view, const char *key) {
  const auto found = view.find(key);
  if (found == view.end() || !found->is_object())
    return Json::array();
  return found->value("values", Json::array());
}
std::string column(int index) {
  std::string value;
  for (++index; index; index = (index - 1) / 26)
    value.insert(value.begin(), char('A' + (index - 1) % 26));
  return value;
}
std::string cell(int row, int col) { return column(col) + std::to_string(row + 1); }
} // namespace
class Grid : public Fl_Table {
  View &owner_;
  Json cells_ = Json::array(), styles_ = Json::array(), palette_ = Json::object(),
       numeric_ = Json::array();
  Json cell_style(int row, int col) const {
    int r = row - origin_row_, c = col - origin_col_;
    if (r < 0 || c < 0 || r >= int(styles_.size()) || c >= int(styles_[r].size()))
      return Json::object();
    auto key = styles_[r][c].dump();
    auto found = palette_.find(key);
    return found == palette_.end() ? Json::object() : *found;
  }
  static Fl_Color rgb(const Json &value, Fl_Color fallback) {
    if (!value.is_string())
      return fallback;
    auto text = value.get<std::string>();
    if (text.size() != 7 || text[0] != '#' ||
        !std::all_of(text.begin() + 1, text.end(),
                     [](unsigned char c) { return std::isxdigit(c); }))
      return fallback;
    unsigned n = std::stoul(text.substr(1), nullptr, 16);
    return fl_rgb_color(n >> 16, (n >> 8) & 255, n & 255);
  }
  int origin_row_ = 0, origin_col_ = 0;
  double zoom_ = 1.;
  Json row_sizes_ = Json::array(), column_sizes_ = Json::array();
  void apply_sizes() {
    for (int i = 0; i < int(row_sizes_.size()) && i < 64; ++i)
      if (origin_row_ + i < rows())
        row_height(origin_row_ + i,
                   std::max(0, int(std::round(row_sizes_[i].get<double>() * zoom_))));
    for (int i = 0; i < int(column_sizes_.size()) && i < 16; ++i)
      if (origin_col_ + i < cols())
        col_width(origin_col_ + i,
                  std::max(0, int(std::round(column_sizes_[i].get<double>() * zoom_))));
  }
  std::string active_, selection_;
  int selected_r1_ = 0, selected_r2_ = 0, selected_c1_ = 0, selected_c2_ = 0;
  bool cell_drag_ = false;
  int resize_row_ = -1, resize_column_ = -1, resize_kind_ = 0;
  static void poll(void *data) {
    auto &self = *static_cast<Grid *>(data);
    if (!self.owner_.busy_ && !self.owner_.pending_ && self.visible_r()) {
      int r1, r2, c1, c2;
      self.visible_cells(r1, r2, c1, c2);
      auto view = self.owner_.snapshot_.value("view", Json::object());
      int row = view.value("row", 0), col = view.value("column", 0);
      if (r1 < row || r2 >= row + 64 || c1 < col || c2 >= col + 16)
        self.owner_.submit_(
            {{"@domed.viewport", {{"row", std::max(0, r1)}, {"column", std::max(0, c1)}}}});
    }
    Fl::repeat_timeout(.15, poll, data);
  }
  int handle(int event) override {
    if (event == FL_PASTE && !owner_.pending_) {
      owner_.submit_(
          {{"edit.paste", {{"text", std::string(Fl::event_text(), Fl::event_length())}}}});
      return 1;
    }
    if (event == FL_KEYDOWN && !owner_.busy_ && !owner_.pending_) {
      int key = Fl::event_key(),
          dr = key == FL_Up     ? -1
               : key == FL_Down ? 1
                                : 0,
          dc = key == FL_Left    ? -1
               : key == FL_Right ? 1
                                 : 0;
      if (dr || dc) {
        owner_.submit_({{"selection.move",
                         {{"dr", dr}, {"dc", dc}, {"extend", bool(Fl::event_state(FL_SHIFT))}}}});
        return 1;
      }
      if (key == FL_Tab) {
        owner_.submit_(
            {{"selection.next", {{"byRow", true}, {"forward", !bool(Fl::event_state(FL_SHIFT))}}}});
        return 1;
      }
      if (key == FL_Enter || key == FL_F + 2) {
        owner_.editor_->take_focus();
        owner_.editor_->insert_position(0, int(std::string(owner_.editor_->value()).size()));
        return 1;
      }
    }
    if (event == FL_PUSH) {
      cell_drag_ = false;
    }
    if (event == FL_PUSH)
      resize_kind_ = Fl::event_y() < y() + col_header_height()  ? 1
                     : Fl::event_x() < x() + row_header_width() ? 2
                                                                : 0;
    int result = Fl_Table::handle(event);
    if (event == FL_RELEASE && !owner_.pending_) {
      if (cell_drag_) {
        int r1, r2, c1, c2;
        get_selection(r1, r2, c1, c2);
        if (r1 >= 0 && c1 >= 0 && r2 >= r1 && c2 >= c1)
          owner_.submit_({{"selection.set", {{"range", cell(r1, c1) + ":" + cell(r2, c2)}}}});
        cell_drag_ = false;
      }
      if (resize_row_ >= 0) {
        auto r = std::to_string(resize_row_ + 1);
        owner_.submit_({{"home.rowHeight",
                         {{"rows", r + ":" + r}, {"height", row_height(resize_row_) / zoom_}}}});
      }
      if (resize_column_ >= 0) {
        auto c = column(resize_column_);
        owner_.submit_({{"home.columnWidth",
                         {{"cols", c + ":" + c}, {"width", col_width(resize_column_) / zoom_}}}});
      }
      resize_row_ = resize_column_ = -1;
    }
    return result;
  }
  void draw_cell(TableContext context, int row, int col, int X, int Y, int W, int H) override {
    if (context == CONTEXT_STARTPAGE) {
      fl_font(labelfont(), labelsize());
      return;
    }
    if (context != CONTEXT_CELL && context != CONTEXT_ROW_HEADER && context != CONTEXT_COL_HEADER)
      return;
    bool header = context != CONTEXT_CELL;
    auto style = header ? Json::object() : cell_style(row, col);
    auto font = style.value("font", Json::object()), align = style.value("align", Json::object());
    fl_push_clip(X, Y, W, H);
    Fl_Color background =
        header ? fl_rgb_color(218, 222, 228) : rgb(style.value("background", Json()), FL_WHITE);
    if (!header && row >= selected_r1_ && row <= selected_r2_ && col >= selected_c1_ &&
        col <= selected_c2_) {
      unsigned char r, g, b, ar, ag, ab;
      Fl::get_color(background, r, g, b);
      Fl::get_color(FL_SELECTION_COLOR, ar, ag, ab);
      background = fl_rgb_color((3 * r + ar) / 4, (3 * g + ag) / 4, (3 * b + ab) / 4);
    }
    fl_color(background);
    fl_rectf(X, Y, W, H);
    std::string value;
    int r = row - origin_row_, c = col - origin_col_;
    if (context == CONTEXT_ROW_HEADER)
      value = std::to_string(row + 1);
    else if (context == CONTEXT_COL_HEADER)
      value = column(col);
    else if (r >= 0 && c >= 0 && r < int(cells_.size()) && cells_[r].is_array() &&
             c < int(cells_[r].size()))
      value = text(cells_[r][c]);
    auto face = header ? artcraft::BitmapFace{labelfont(), labelsize(), false, false}
                       : artcraft::document_bitmap_face(
                             font.value("name", std::string()), font.value("bold", false),
                             font.value("italic", false),
                             int(std::round(font.value("size", 12.) * 4. / 3. * zoom_)));
    fl_font(face.font, face.pixels);
    fl_color(rgb(style.value("foreground", Json()), FL_BLACK));
    Fl_Align flags = FL_ALIGN_INSIDE | FL_ALIGN_CLIP;
    auto horizontal = align.value("h", std::string("General")),
         vertical = align.value("v", std::string("Bottom"));
    bool number = r >= 0 && c >= 0 && r < int(numeric_.size()) && c < int(numeric_[r].size()) &&
                  numeric_[r][c].is_boolean() && numeric_[r][c].get<bool>();
    flags |= horizontal == "Center" || horizontal == "CenterAcross"         ? FL_ALIGN_CENTER
             : horizontal == "Right" || (horizontal == "General" && number) ? FL_ALIGN_RIGHT
                                                                            : FL_ALIGN_LEFT;
    flags |= vertical == "Top"                              ? FL_ALIGN_TOP
             : vertical == "Center" || vertical == "Middle" ? FL_ALIGN_CENTER
                                                            : FL_ALIGN_BOTTOM;
    if (align.value("wrap", false))
      flags |= FL_ALIGN_WRAP;
    int indent = int(align.value("indent", 0) * 9 * zoom_), padding = std::max(2, int(4 * zoom_));
    fl_draw(value.c_str(), X + padding + indent, Y + 2, std::max(1, W - 2 * padding - indent),
            std::max(1, H - 4), flags, nullptr, 0);
    if (!header && font.value("bold", false) && !face.bold)
      fl_draw(value.c_str(), X + padding + indent + 1, Y + 2, std::max(1, W - 2 * padding - indent),
              std::max(1, H - 4), flags, nullptr, 0);
    if (!header &&
        (font.value("strike", false) || font.value("underline", std::string("None")) != "None")) {
      int length = std::min(int(fl_width(value.c_str())), std::max(0, W - 2 * padding - indent)),
          left = X + padding + indent;
      if (flags & FL_ALIGN_RIGHT)
        left = X + W - padding - length;
      else if (!(flags & FL_ALIGN_LEFT))
        left = X + (W - length) / 2;
      int baseline = vertical == "Top"                              ? Y + 2 + face.pixels
                     : vertical == "Center" || vertical == "Middle" ? Y + (H + face.pixels) / 2
                                                                    : Y + H - 3;
      if (font.value("strike", false))
        fl_line(left, baseline - face.pixels / 3, left + length, baseline - face.pixels / 3);
      if (font.value("underline", std::string("None")) != "None")
        fl_line(left, baseline, left + length, baseline);
    }
    fl_color(fl_rgb_color(180, 185, 190));
    fl_rect(X, Y, W, H);
    if (!header) {
      auto borders = style.value("borders", Json::object()),
           colors = style.value("borderColors", Json::object());
      for (const auto &side : {"top", "bottom", "left", "right"}) {
        auto border = borders.value(side, Json::object());
        auto kind = border.value("style", std::string("None"));
        if (kind == "None")
          continue;
        fl_color(rgb(colors.value(side, Json()), FL_BLACK));
        fl_line_style(kind.find("Dash") != std::string::npos ? FL_DASH : FL_SOLID,
                      kind == "Thick"    ? 3
                      : kind == "Medium" ? 2
                                         : 1);
        if (std::string(side) == "top")
          fl_line(X, Y, X + W, Y);
        else if (std::string(side) == "bottom")
          fl_line(X, Y + H - 1, X + W, Y + H - 1);
        else if (std::string(side) == "left")
          fl_line(X, Y, X, Y + H);
        else
          fl_line(X + W - 1, Y, X + W - 1, Y + H);
      }
      fl_line_style(0);
      if (active_ == cell(row, col)) {
        fl_color(FL_SELECTION_COLOR);
        fl_rect(X + 1, Y + 1, W - 2, H - 2);
      }
    }
    fl_pop_clip();
  }

public:
  Grid(int x, int y, int w, int h, View &owner) : Fl_Table(x, y, w, h), owner_(owner) {
    rows(1);
    row_height(0, 20);
    rows(1048576);
    cols(1);
    col_width(0, 64);
    cols(16384);
    row_header(1);
    col_header(1);
    row_header_width(56);
    col_header_height(28);
    col_resize(1);
    row_resize(1);
    end();
    callback([](Fl_Widget *widget, void *) {
      auto &self = *static_cast<Grid *>(widget);
      if (self.callback_context() == CONTEXT_RC_RESIZE && self.is_interactive_resize()) {
        if (self.resize_kind_ == 1 && self.resize_column_ < 0)
          self.resize_column_ = self.callback_col();
        if (self.resize_kind_ == 2 && self.resize_row_ < 0)
          self.resize_row_ = self.callback_row();
        return;
      }
      if (self.callback_context() == CONTEXT_CELL && Fl::event() == FL_DRAG &&
          !self.owner_.pending_) {
        self.cell_drag_ = true;
        return;
      }
      if (self.callback_context() == CONTEXT_CELL && Fl::event() == FL_PUSH && !self.owner_.busy_ &&
          !self.owner_.pending_)
        self.owner_.submit_(
            {{"selection.set", {{"range", cell(self.callback_row(), self.callback_col())}}}});
    });
    when(FL_WHEN_RELEASE | FL_WHEN_CHANGED);
    Fl::add_timeout(.15, poll, this);
  }
  void zoom(double value) {
    if (std::abs(value - zoom_) < .001)
      return;
    zoom_ = value;
    row_height_all(std::max(14, int(20 * value)));
    col_width_all(std::max(30, int(64 * value)));
    apply_sizes();
    redraw();
  }
  void refresh(const Json &snapshot) {
    auto view = snapshot.value("view", Json::object());
    cells_ = value_rows(view, "cells");
    auto cells = view.value("cells", Json::object());
    styles_ = cells.value("styles", Json::array());
    palette_ = cells.value("stylePalette", Json::object());
    numeric_ = cells.value("numeric", Json::array());
    origin_row_ = view.value("row", 0);
    origin_col_ = view.value("column", 0);
    row_sizes_ = cells.value("rowHeights", Json::array());
    column_sizes_ = cells.value("columnWidths", Json::array());
    apply_sizes();
    row_header_width(origin_row_ > 99999 ? 72 : 56);
    auto selected =
        snapshot.value("document", Json::object()).value("selection", std::string("A1"));
    if (selected != selection_) {
      selection_ = selected;
      auto parse = [](std::string address) -> std::pair<int, int> {
        int row = 0, col = 0;
        size_t at = 0;
        while (at < address.size() && address[at] >= 'A' && address[at] <= 'Z') {
          col = col * 26 + address[at++] - 'A' + 1;
          if (col > 16384)
            return {-1, -1};
        }
        while (at < address.size() && address[at] >= '0' && address[at] <= '9') {
          row = row * 10 + address[at++] - '0';
          if (row > 1048576)
            return {-1, -1};
        }
        if (at != address.size())
          return {-1, -1};
        return {row ? row - 1 : 0, col ? col - 1 : 0};
      };
      auto comma = selected.find(',');
      auto range = selected.substr(0, comma);
      auto colon = range.find(':');
      auto [r1, c1] = parse(range.substr(0, colon));
      auto [r2, c2] = parse(colon == std::string::npos ? range : range.substr(colon + 1));
      if (r1 >= 0 && c1 >= 0 && r2 >= 0 && c2 >= 0) {
        selected_r1_ = std::min(r1, r2);
        selected_r2_ = std::max(r1, r2);
        selected_c1_ = std::min(c1, c2);
        selected_c2_ = std::max(c1, c2);
        set_selection(selected_r1_, selected_c1_, selected_r2_, selected_c2_);
      }
    }
    auto active = snapshot.value("document", Json::object()).value("activeCell", std::string("A1"));
    if (active != active_) {
      active_ = active;
      size_t at = 0;
      int col = 0, row = 0;
      while (at < active.size() && active[at] >= 'A' && active[at] <= 'Z') {
        col = col * 26 + active[at++] - 'A' + 1;
        if (col > 16384)
          return;
      }
      while (at < active.size() && active[at] >= '0' && active[at] <= '9') {
        row = row * 10 + active[at++] - '0';
        if (row > 1048576)
          return;
      }
      if (at == active.size() && row > 0 && col > 0) {
        int r1, r2, c1, c2;
        visible_cells(r1, r2, c1, c2);
        if (row - 1 < r1 || row - 1 > r2)
          row_position(row - 1);
        if (col - 1 < c1 || col - 1 > c2)
          col_position(col - 1);
      }
    }
    redraw();
  }
  ~Grid() { Fl::remove_timeout(poll, this); }
};
class SlideList : public Fl_Table {
  View &owner_;
  std::map<int, GpuImage> images_;
  Json slides_ = Json::array();
  int active_ = 0;
  void draw_cell(TableContext context, int row, int, int X, int Y, int W, int H) override {
    if (context != CONTEXT_CELL || row < 0 || row >= int(slides_.size()))
      return;
    fl_push_clip(X, Y, W, H);
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(X, Y, W, H);
    fl_color(active_ == row ? FL_SELECTION_COLOR : FL_DARK3);
    fl_rect(X + 4, Y + 4, W - 8, H - 8);
    if (auto image = images_.find(row); image != images_.end() && !image->second.empty()) {
      double scale =
          std::min(double(W - 26) / image->second.width(), double(H - 30) / image->second.height());
      int iw = int(image->second.width() * scale), ih = int(image->second.height() * scale);
      int left = std::max(X, x()), top = std::max(Y, y()),
          right = std::min(X + W, x() + w() - Fl::scrollbar_size()),
          bottom = std::min(Y + H, y() + h());
      image->second.draw(*this, {X + (W - iw) / 2, Y + 8, iw, ih},
                         {left, top, std::max(0, right - left), std::max(0, bottom - top)});
    }
    fl_color(FL_FOREGROUND_COLOR);
    fl_font(labelfont(), labelsize());
    auto label = std::to_string(row + 1) + "  " + slides_[row].value("title", std::string());
    fl_draw(label.c_str(), X + 8, Y + H - 20, W - 16, 18,
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Up || Fl::event_key() == FL_Down) &&
        !owner_.busy_) {
      owner_.submit_({{"slide.go",
                       {{"index", std::clamp(active_ + (Fl::event_key() == FL_Up ? -1 : 1), 0,
                                             std::max(0, int(slides_.size()) - 1))}}}});
      return 1;
    }
    return Fl_Table::handle(event);
  }

public:
  SlideList(int x, int y, int w, int h, View &owner) : Fl_Table(x, y, w, h), owner_(owner) {
    cols(1);
    col_width(0, w - Fl::scrollbar_size() - 4);
    rows(1);
    row_height(0, 122);
    end();
    callback([](Fl_Widget *widget, void *) {
      auto &self = *static_cast<SlideList *>(widget);
      if (self.callback_context() == CONTEXT_CELL && Fl::event() == FL_PUSH && !self.owner_.busy_)
        self.owner_.submit_({{"slide.go", {{"index", self.callback_row()}}}});
    });
    when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
  }
  void refresh(const Json &snapshot) {
    slides_ = snapshot.value("document", Json::object()).value("slides", Json::array());
    rows(int(slides_.size()));
    active_ = snapshot.value("document", Json::object())
                  .value("selection", Json::object())
                  .value("slide", 0);
    for (const auto &image :
         snapshot.value("view", Json::object()).value("thumbnails", Json::array()))
      images_[image.at("slide").get<int>()].set(image.at("png"));
    for (auto i = images_.begin(); i != images_.end();)
      if (i->first >= int(slides_.size()))
        i = images_.erase(i);
      else
        ++i;
    redraw();
  }
  void resize(int x, int y, int w, int h) override {
    Fl_Table::resize(x, y, w, h);
    col_width(0, std::max(40, w - Fl::scrollbar_size() - 4));
  }
};
class Canvas : public Fl_Widget {
  View &owner_;
  struct PageImage {
    std::unique_ptr<Fl_PNG_Image> image;
    std::string encoded;
  };
  std::map<int, PageImage> word_images_;
  WordLayout word_layout_;
  std::string word_epoch_, word_focus_;
  int requested_page_ = -1;
  static void word_poll(void *data) {
    auto &c = *static_cast<Canvas *>(data);
    c.poll_word();
    Fl::repeat_timeout(.2, word_poll, data);
  }
  std::unique_ptr<Fl_PNG_Image> raster_;
  std::string encoded_;
  Json gesture_ = Json::array();
  int clicks_ = 0;
  bool extend_ = false;
  PageRect deck_rect_;
  double deck_scale_ = 1.;
  void gpu_clip() {
    auto *win = dynamic_cast<Fl_Gl_Window *>(window());
    if (!win)
      return;
    auto *port = owner_.scroll_;
    int sx = std::max(x(), port->x()), sy = std::max(y(), port->y());
    int right = std::min(x() + w(), port->x() + port->w() - Fl::scrollbar_size());
    int bottom = std::min(y() + h(), port->y() + port->h() - Fl::scrollbar_size());
    const double scale = win->pixels_per_unit();
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(sx * scale), int((win->h() - bottom) * scale),
              std::max(0, int((right - sx) * scale)), std::max(0, int((bottom - sy) * scale)));
  }
  void gpu_image(Fl_PNG_Image *image = nullptr, int left = 0, int top = 0, int width = 0,
                 int height = 0) {
    if (!image) {
      image = raster_.get();
      if (!image)
        return;
      left = x() + 12;
      top = y() + 12;
      width = image->w();
      height = image->h();
      if (owner_.project_ == "deckcraft" && !owner_.presentation_) {
        left = x() + int(deck_rect_.x);
        top = y() + int(deck_rect_.y);
        width = int(deck_rect_.w);
        height = int(deck_rect_.h);
      }
      if (owner_.presentation_) {
        double k = std::min(double(w()) / width, double(h()) / height);
        width = std::max(1, int(width * k));
        height = std::max(1, int(height * k));
        left = x() + (w() - width) / 2;
        top = y() + (h() - height) / 2;
      }
    }
    const int depth = image->d();
    if (depth != 3 && depth != 4)
      throw std::runtime_error("Unsupported preview pixel format");
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT |
                 GL_PIXEL_MODE_BIT);
    gpu_clip();
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, depth == 4 ? GL_RGBA : GL_RGB, image->data_w(), image->data_h(),
                 0, depth == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, image->array);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const int right = left + width, bottom = top + height;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2i(left, top);
    glTexCoord2f(1, 0);
    glVertex2i(right, top);
    glTexCoord2f(1, 1);
    glVertex2i(right, bottom);
    glTexCoord2f(0, 1);
    glVertex2i(left, bottom);
    glEnd();
    glDeleteTextures(1, &texture);
    glPopClientAttrib();
    glPopAttrib();
  }
  struct Hit {
    int x, y, w, h, index;
  };
  std::vector<Hit> hits_;
  void label_text(const std::string &value, int x, int y, int w, int h) {
    // Canonical DomeD styles set labelfont/labelsize to the installed BDF strike.
    fl_font(labelfont(), labelsize());
    fl_draw(value.c_str(), x, y, w, h,
            FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP, nullptr, 0);
  }
  void draw() override {
    if (owner_.project_ == "wordcraft") {
      word_draw();
      return;
    }
    fl_push_clip(x(), y(), w(), h());

    gpu_clip();
    fl_color(owner_.project_ == "deckcraft" ? fl_darker(FL_BACKGROUND_COLOR)
                                            : fl_rgb_color(250, 250, 245));
    fl_rectf(x(), y(), w(), h());
    fl_color(FL_BLACK);
    hits_.clear();
    const auto &doc = owner_.snapshot_.value("document", Json::object());
    const auto &view = owner_.snapshot_.value("view", Json::object());
    if (owner_.presentation_ && view.value("raster", Json::object()).value("ended", false)) {
      fl_color(FL_BLACK);
      fl_rectf(x(), y(), w(), h());
      fl_color(FL_WHITE);
      label_text("End of presentation. Press Escape to return.", x() + 30, y() + 40, w() - 60, 100);
      fl_pop_clip();
      return;
    }
    if (raster_ && (owner_.project_ != "gridcraft" || view.value("preview_mode", false))) {
      gpu_image();
      if (owner_.project_ == "wordcraft") {
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
        gpu_clip();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        unsigned char red, green, blue;
        Fl::get_color(FL_SELECTION_COLOR, red, green, blue);
        glColor4f(red / 255.f, green / 255.f, blue / 255.f, .25f);
        for (const auto &item :
             view.value("layout", Json::object()).value("selection", Json::array()))
          if (item.value("page", 0) == view.value("raster", Json::object()).value("page", 0)) {
            auto rect = item.at("rect");
            float left = x() + 12 + rect.value("x", 0.f) * 4.f / 3.f,
                  top = y() + 12 + rect.value("y", 0.f) * 4.f / 3.f;
            glRectf(left, top, left + rect.value("w", 0.f) * 4.f / 3.f,
                    top + rect.value("h", 0.f) * 4.f / 3.f);
          }
        glPopAttrib();
        auto caret = view.value("layout", Json::object()).value("caret", Json());
        if (caret.is_object() &&
            caret.value("page", 0) == view.value("raster", Json::object()).value("page", 0)) {
          fl_color(FL_SELECTION_COLOR);
          int cx = x() + 12 + int(caret.value("x", 0.) * 4. / 3.),
              cy = y() + 12 + int(caret.value("y", 0.) * 4. / 3.);
          fl_rectf(cx, cy, 2, std::max(2, int(caret.value("h", 12.) * 4. / 3.)));
        }
      }
      fl_pop_clip();
      return;
    }

    if (owner_.project_ == "gridcraft") {
      auto values = value_rows(view, "cells");
      const int cw = std::max(32, (w() - 40) / 8), rh = std::max(26, labelsize() + 10);
      for (int c = 0; c < 8; ++c)
        label_text(std::string(1, char('A' + c)), x() + 40 + c * cw, y() + 2, cw, rh);
      for (int r = 0; r < std::min(100, (h() - rh) / rh); ++r) {
        label_text(std::to_string(r + 1), x() + 2, y() + (r + 1) * rh, 36, rh);
        for (int c = 0; c < 8; ++c) {
          int cx = x() + 40 + c * cw, cy = y() + (r + 1) * rh;
          fl_color(fl_rgb_color(190, 190, 185));
          fl_rect(cx, cy, cw, rh);
          if (owner_.row_ == r && owner_.col_ == c) {
            fl_color(FL_SELECTION_COLOR);
            fl_rect(cx + 1, cy + 1, cw - 2, rh - 2);
          }
          fl_color(FL_BLACK);
          std::string value;
          if (r < int(values.size()) && values[r].is_array() && c < int(values[r].size()))
            value = text(values[r][c]);
          fl_push_clip(cx + 3, cy + 2, cw - 6, rh - 4);
          label_text(value, cx + 3, cy + 2, cw - 6, rh - 4);
          fl_pop_clip();
          hits_.push_back({cx, cy, cw, rh, r * 16 + c});
        }
      }
    } else if (owner_.project_ == "wordcraft") {
      int cy = y() + 20;
      for (const auto &b : doc.value("blocks", Json::array())) {
        if (b.value("index", -1) < std::max(0, owner_.block_ - 2))
          continue;
        if (cy > y() + h())
          break;
        const auto value = b.value("text", std::string("[" + b.value("type", "object") + "]"));
        int width = std::max(20, w() - 48), height = 0;
        fl_font(labelfont(), labelsize());
        fl_measure(value.c_str(), width, height, 0);
        height = std::max(labelsize() + 12, height + 12);
        if (b.value("index", -1) == owner_.block_) {
          fl_color(fl_rgb_color(220, 235, 255));
          fl_rectf(x() + 16, cy, w() - 32, height);
        }
        fl_color(FL_BLACK);
        label_text(value, x() + 24, cy + 6, w() - 48, height - 8);
        hits_.push_back({x() + 16, cy, w() - 32, height, b.value("index", -1)});
        cy += height;
      }
    } else {
      auto slide = view.value("slide", Json::object());
      const auto size = doc.value("slideSize", Json::array({960., 540.}));
      const double scale =
          std::min((w() - 24) / size[0].get<double>(), (h() - 24) / size[1].get<double>());
      int index = 0;
      for (const auto &shape : slide.value("shapes", Json::array())) {
        // Geometry remains in upstream slide units; bitmap text stays at its native strike.
        auto box = shape.value("box", Json::array());
        double sx = shape.value("x", 0.), sy = shape.value("y", 0.), sw = shape.value("w", 200.),
               sh = shape.value("h", 80.);
        if (box.is_array() && box.size() >= 4) {
          sx = box[0];
          sy = box[1];
          sw = box[2];
          sh = box[3];
        }
        if (box.is_object()) {
          sx = box.value("x", sx);
          sy = box.value("y", sy);
          sw = box.value("w", sw);
          sh = box.value("h", sh);
        }
        const int cx = x() + 12 + int(sx * scale), cy = y() + 12 + int(sy * scale),
                  cw = std::max(1, int(sw * scale)), ch = std::max(1, int(sh * scale));
        fl_color(shape.value("id", Json()) == owner_.shape_id_ ? FL_SELECTION_COLOR
                                                               : fl_rgb_color(150, 150, 150));
        fl_rect(cx, cy, cw, ch);
        fl_color(FL_BLACK);
        fl_push_clip(cx, cy, cw, ch);
        label_text(shape.value("text", std::string()), cx + 4, cy + 4, std::max(1, cw - 8),
                   std::max(1, ch - 8));
        fl_pop_clip();
        hits_.push_back({cx, cy, cw, ch, index++});
      }
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    if (owner_.project_ == "wordcraft" && event == FL_MOUSEWHEEL && Fl::event_state(FL_CTRL)) {
      owner_.submit_({{Fl::event_dy() > 0 ? "view.zoomOut" : "view.zoomIn", Json::object()}});
      return 1;
    }
    if (event == FL_FOCUS || event == FL_UNFOCUS)
      return 1;
    if (owner_.presentation_ && (event == FL_KEYDOWN || event == FL_PUSH)) {
      std::string action;
      int key = Fl::event_key();
      if (key == FL_Escape)
        action = "exit";
      else if (key == FL_Left || key == FL_Page_Up || key == FL_BackSpace)
        action = "previous";
      else if (event == FL_PUSH || key == FL_Right || key == FL_Page_Down || key == ' ' ||
               key == FL_Enter)
        action = "next";
      if (!action.empty()) {
        owner_.submit_({{"@domed.show", {{"action", action}}}});
        return 1;
      }
    }
    if (event == FL_PASTE && owner_.project_ != "gridcraft" && !owner_.pending_) {
      owner_.submit_(
          {{"edit.paste", {{"text", std::string(Fl::event_text(), Fl::event_length())}}}});
      return 1;
    }
    if (event == FL_KEYDOWN && owner_.project_ != "gridcraft" && Fl::event_state(FL_CTRL) &&
        Fl::event_key() == 'v') {
      Fl::paste(*this, 1);
      return 1;
    }
    if (event == FL_KEYDOWN && owner_.project_ == "deckcraft" && !owner_.pending_) {
      int key = Fl::event_key();
      std::string command;
      Json params = Json::object();
      if (key == FL_Page_Down)
        command = "slide.next";
      else if (key == FL_Page_Up)
        command = "slide.previous";
      else if (key == FL_Escape) {
        command = "text.exit";
        owner_.deck_editing_ = false;
      } else if (!owner_.shape_id_.is_null()) {
        if (key == FL_BackSpace) {
          command = "text.delete";
          params["dir"] = "backward";
        } else if (key == FL_Delete) {
          command = "text.delete";
          params["dir"] = "forward";
        } else if (key == FL_Enter) {
          command = "text.insert";
          params["text"] = "\n";
        } else if (key == FL_Left || key == FL_Right || key == FL_Up || key == FL_Down) {
          command = "text.move";
          params["to"] = key == FL_Left    ? "left"
                         : key == FL_Right ? "right"
                         : key == FL_Up    ? "up"
                                           : "down";
          params["extend"] = bool(Fl::event_state(FL_SHIFT));
        } else if (!Fl::event_state(FL_CTRL | FL_ALT | FL_META) && Fl::event_length() > 0) {
          command = "text.insert";
          params["text"] = std::string(Fl::event_text(), Fl::event_length());
        }
      }
      if (!command.empty()) {
        std::vector<std::pair<std::string, Json>> commands;
        if (command.starts_with("text.") && command != "text.exit" && !owner_.deck_editing_) {
          commands.push_back({"text.edit", {{"id", owner_.shape_id_}, {"end", true}}});
          owner_.deck_editing_ = true;
        }
        commands.push_back({command, params});
        owner_.submit_(std::move(commands));
        return 1;
      }
    }
    if (event == FL_KEYDOWN && owner_.project_ == "wordcraft" && !owner_.pending_) {
      const int key = Fl::event_key();
      std::string command;
      Json params = Json::object();
      if (Fl::event_state(FL_CTRL) && key == FL_Enter) {
        owner_.submit_({{"insert.pageBreak", Json::object()}});
        return 1;
      }
      if (Fl::event_state(FL_CTRL) &&
          (key == FL_Home || key == FL_End || key == FL_Left || key == FL_Right)) {
        owner_.submit_({{key == FL_Home   ? "caret.docStart"
                         : key == FL_End  ? "caret.docEnd"
                         : key == FL_Left ? "caret.wordLeft"
                                          : "caret.wordRight",
                         {{"extend", bool(Fl::event_state(FL_SHIFT))}}}});
        return 1;
      }
      if (Fl::event_state(FL_CTRL | FL_ALT | FL_META))
        return 0;
      if (key == FL_Tab) {
        owner_.submit_({{"text.insertTab", Json::object()}});
        return 1;
      }
      if (key == FL_BackSpace)
        command = "text.backspace";
      else if (key == FL_Delete)
        command = "text.delete";
      else if (key == FL_Enter || key == FL_KP_Enter)
        command = "text.newParagraph";
      else if (key == FL_Left)
        command = "caret.left";
      else if (key == FL_Right)
        command = "caret.right";
      else if (key == FL_Up)
        command = "caret.up";
      else if (key == FL_Down)
        command = "caret.down";
      else if (key == FL_Page_Up)
        command = "caret.pageUp";
      else if (key == FL_Page_Down)
        command = "caret.pageDown";
      else if (key == FL_Home)
        command = "caret.home";
      else if (key == FL_End)
        command = "caret.end";
      else if (Fl::event_length() > 0) {
        command = "text.insert";
        params["text"] = std::string(Fl::event_text(), Fl::event_length());
      }
      if (!command.empty()) {
        if (command.starts_with("caret."))
          params["extend"] = bool(Fl::event_state(FL_SHIFT));
        owner_.submit_({{command, params}});
        return 1;
      }
    }
    if (event == FL_PUSH && !owner_.busy_) {
      Fl::focus(this);
      if (!word_layout_.pages.empty() && owner_.project_ == "wordcraft" && !owner_.pending_) {
        auto *page = word_layout_.hit(Fl::event_x() - x(), Fl::event_y() - y());
        if (!page)
          return 1;
        Json point = {{"page", page->page},
                      {"x", (Fl::event_x() - x() - page->x) / word_layout_.scale},
                      {"y", (Fl::event_y() - y() - page->y) / word_layout_.scale}};
        gesture_ = Json::array({point});
        clicks_ = Fl::event_clicks();
        extend_ = bool(Fl::event_state(FL_SHIFT));
        return 1;
      }

      if (raster_ && owner_.project_ == "deckcraft" && !owner_.pending_) {
        gesture_ = Json::array({{{"kind", Fl::event_clicks() ? "doubleclick" : "down"},
                                 {"x", (Fl::event_x() - x() - deck_rect_.x) * .75 / deck_scale_},
                                 {"y", (Fl::event_y() - y() - deck_rect_.y) * .75 / deck_scale_}}});
        return 1;
      }

      for (auto i = hits_.rbegin(); i != hits_.rend(); ++i)
        if (Fl::event_x() >= i->x && Fl::event_x() < i->x + i->w && Fl::event_y() >= i->y &&
            Fl::event_y() < i->y + i->h) {
          if (owner_.project_ == "gridcraft" && !owner_.pending_)
            owner_.submit_({{"selection.set", {{"range", cell(i->index / 16, i->index % 16)}}}});
          else
            owner_.pick(i->index);
          return 1;
        }
    }
    if ((event == FL_DRAG || event == FL_RELEASE) && !gesture_.empty() &&
        owner_.project_ == "wordcraft") {
      auto *page = word_layout_.hit(Fl::event_x() - x(), Fl::event_y() - y());
      if (!page) {
        int i = gesture_.at(0).at("page");
        if (i >= 0 && i < int(word_layout_.pages.size()))
          page = &word_layout_.pages[i];
      }
      if (!page)
        return 1;
      Json point = {{"page", page->page},
                    {"x", std::clamp((Fl::event_x() - x() - page->x) / word_layout_.scale, 0.,
                                     page->w / word_layout_.scale)},
                    {"y", std::clamp((Fl::event_y() - y() - page->y) / word_layout_.scale, 0.,
                                     page->h / word_layout_.scale)}};
      if (event == FL_RELEASE) {
        auto from = gesture_.at(0);
        gesture_ = Json::array();
        owner_.submit_(
            {{"@domed.wordSelection",
              {{"from", from}, {"to", point}, {"clicks", clicks_}, {"extend", extend_}}}});
      }
      return 1;
    }
    if ((event == FL_DRAG || event == FL_RELEASE) && !gesture_.empty() &&
        owner_.project_ == "deckcraft") {
      Json point = {{"kind", event == FL_RELEASE ? "up" : "drag"},
                    {"x", (Fl::event_x() - x() - deck_rect_.x) * .75 / deck_scale_},
                    {"y", (Fl::event_y() - y() - deck_rect_.y) * .75 / deck_scale_}};
      if (gesture_.size() > 1 && event == FL_DRAG)
        gesture_.back() = point;
      else
        gesture_.push_back(point);
      if (event == FL_RELEASE) {
        auto events = std::move(gesture_);
        gesture_ = Json::array();
        owner_.submit_({{"@domed.pointer", {{"events", events}}}});
      }
      return 1;
    }
    return Fl_Widget::handle(event);
  }
  void word_draw() {
    fl_push_clip(x(), y(), w(), h());
    gpu_clip();
    fl_color(fl_darker(FL_BACKGROUND_COLOR));
    fl_rectf(x(), y(), w(), h());
    auto view = owner_.snapshot_.value("view", Json::object());
    auto layout = view.value("layout", Json::object());
    for (const auto &p : word_layout_.pages) {
      int left = x() + int(p.x), top = y() + int(p.y), pw = int(p.w), ph = int(p.h);
      if (left + pw < owner_.scroll_->x() || left > owner_.scroll_->x() + owner_.scroll_->w() ||
          top + ph < owner_.scroll_->y() || top > owner_.scroll_->y() + owner_.scroll_->h())
        continue;
      fl_color(fl_darker(FL_BACKGROUND_COLOR));
      fl_rectf(left + 3, top + 3, pw, ph);
      fl_color(FL_WHITE);
      fl_rectf(left, top, pw, ph);
      auto image = word_images_.find(p.page);
      if (image != word_images_.end())
        gpu_image(image->second.image.get(), left, top, pw, ph);
      else {
        fl_color(FL_DARK3);
        label_text("Loading page " + std::to_string(p.page + 1) + "...", left + 24, top + 24,
                   std::max(20, pw - 48), 30);
      }
      fl_color(FL_DARK3);
      fl_rect(left - 1, top - 1, pw + 2, ph + 2);
      glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
      gpu_clip();
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      unsigned char r, g, b;
      Fl::get_color(FL_SELECTION_COLOR, r, g, b);
      glColor4f(r / 255.f, g / 255.f, b / 255.f, .25f);
      for (const auto &item : layout.value("selection", Json::array()))
        if (item.value("page", 0) == p.page) {
          auto rect = item.at("rect");
          float l = left + rect.value("x", 0.f) * word_layout_.scale,
                t = top + rect.value("y", 0.f) * word_layout_.scale;
          glRectf(l, t, l + rect.value("w", 0.f) * word_layout_.scale,
                  t + rect.value("h", 0.f) * word_layout_.scale);
        }
      glPopAttrib();
      auto caret = layout.value("caret", Json());
      if (caret.is_object() && caret.value("page", 0) == p.page) {
        fl_color(FL_SELECTION_COLOR);
        fl_rectf(left + int(caret.value("x", 0.) * word_layout_.scale),
                 top + int(caret.value("y", 0.) * word_layout_.scale), 1,
                 std::max(2, int(caret.value("h", 12.) * word_layout_.scale)));
      }
    }
    fl_pop_clip();
  }

public:
  const WordLayout &geometry() const { return word_layout_; }
  double deck_scale() const { return deck_scale_; }
  void relayout_deck() {
    if (owner_.project_ != "deckcraft" || !raster_)
      return;
    int width = owner_.scroll_->w() - Fl::scrollbar_size(),
        height = owner_.scroll_->h() - Fl::scrollbar_size();
    deck_scale_ = owner_.zoom_ > 0 ? owner_.zoom_
                                   : std::min(double(width - 40) / raster_->w(),
                                              double(height - 40) / raster_->h());
    deck_scale_ = std::clamp(deck_scale_, .05, 5.);
    int rw = int(raster_->w() * deck_scale_), rh = int(raster_->h() * deck_scale_);
    size(std::max(width, rw + 40), std::max(height, rh + 40));
    deck_rect_ = {double(w() - rw) / 2, double(h() - rh) / 2, double(rw), double(rh), 0};
    redraw();
  }
  void relayout_word() {
    if (owner_.project_ != "wordcraft")
      return;
    auto view = owner_.snapshot_.value("view", Json::object());
    auto settings = view.value("settings", Json::object());
    std::vector<PageRect> sizes;
    int i = 0;
    for (const auto &p : view.value("layout", Json::object()).value("pages", Json::array())) {
      if (i >= 10000)
        break;
      sizes.push_back({0, 0, p.value("w", 612.), p.value("h", 792.), i++});
    }
    word_layout_ = WordLayout::place(
        sizes, owner_.scroll_->w() - Fl::scrollbar_size(),
        owner_.scroll_->h() - Fl::scrollbar_size(), settings.value("zoom", 1.),
        settings.value("fit", std::string()), settings.value("singleColumn", false),
        settings.value("multiPage", false));
    size(int(word_layout_.width), int(word_layout_.height));
    redraw();
  }
  void poll_word() {
    if (owner_.busy_ || owner_.pending_ || !visible_r() || word_layout_.pages.empty())
      return;
    auto *s = owner_.scroll_;
    int target = -1;
    auto *center = word_layout_.hit(s->x() + s->w() / 2 - x(), s->y() + s->h() / 2 - y());
    if (center)
      owner_.visible_page_ = center->page;
    if (owner_.office_ui_)
      owner_.office_ui_->geometry(owner_.visible_page_, word_layout_.scale);
    for (const auto &p : word_layout_.pages)
      if (y() + p.y + p.h > s->y() && y() + p.y < s->y() + s->h() && x() + p.x + p.w > s->x() &&
          x() + p.x < s->x() + s->w() && !word_images_.contains(p.page)) {
        target = p.page;
        break;
      }
    if (target >= 0 && target != requested_page_) {
      requested_page_ = target;
      owner_.submit_({{"@domed.pagePreview", {{"page", target}}}});
    }
    if (owner_.horizontal_)
      owner_.redraw();
  }
  void word_raster(const Json &view) {
    const auto epoch = view.value("raster_epoch", std::string());
    if (epoch != word_epoch_) {
      word_images_.clear();
      word_epoch_ = epoch;
    }
    requested_page_ = -1;
    Json images = view.value("rasters", Json::array());
    if (images.empty() && view.contains("raster"))
      images.push_back(view.at("raster"));
    for (const auto &r : images) {
      auto encoded = r.value("png", std::string());
      if (encoded.empty())
        continue;
      int index = r.value("page", 0);
      auto found = word_images_.find(index);
      if (found != word_images_.end() && found->second.encoded == encoded)
        continue;
      auto bytes = artcraft::decode_base64(encoded, 16 * 1024 * 1024);
      artcraft::png_dimensions(bytes);
      auto image = std::make_unique<Fl_PNG_Image>(nullptr, bytes.data(), int(bytes.size()));
      if (image->fail())
        throw std::runtime_error("Cannot decode bitmap page");
      word_images_[index] = {std::move(image), std::move(encoded)};
    }
    auto cache_bytes = [this] {
      size_t total = 0;
      for (const auto &[page, image] : word_images_) {
        (void)page;
        total += size_t(image.image->data_w()) * image.image->data_h() * image.image->d();
      }
      return total;
    };
    while (word_images_.size() > 1 &&
           (word_images_.size() > 8 || cache_bytes() > 64 * 1024 * 1024)) {
      int active = view.value("raster", Json::object()).value("page", 0);
      auto far = word_images_.begin();
      for (auto it = word_images_.begin(); it != word_images_.end(); ++it)
        if (std::abs(it->first - active) > std::abs(far->first - active))
          far = it;
      word_images_.erase(far);
    }
    relayout_word();
    if (view.value("reveal_page", false)) {
      int page = view.value("raster", Json::object()).value("page", 0);
      if (page >= 0 && page < int(word_layout_.pages.size()))
        owner_.scroll_->scroll_to(0, std::max(0, int(word_layout_.pages[page].y) - 20));
    }
    auto focus = owner_.snapshot_.value("document", Json::object())
                     .value("selection", Json::object())
                     .value("focus", Json::object())
                     .dump();
    auto caret = view.value("layout", Json::object()).value("caret", Json());
    if (focus != word_focus_ && caret.is_object()) {
      word_focus_ = focus;
      int page = caret.value("page", 0);
      if (page >= 0 && page < int(word_layout_.pages.size())) {
        auto p = word_layout_.pages[page];
        auto *s = owner_.scroll_;
        int cx = int(p.x + caret.value("x", 0.) * word_layout_.scale),
            cy = int(p.y + caret.value("y", 0.) * word_layout_.scale);
        int sx = s->xposition(), sy = s->yposition();
        if (cy < sy + 16 || cy > sy + s->h() - 40)
          sy = std::max(0, cy - s->h() / 3);
        if (cx < sx + 16 || cx > sx + s->w() - 40)
          sx = std::max(0, cx - s->w() / 2);
        s->scroll_to(sx, sy);
      }
    }
  }
  Canvas(int x, int y, int w, int h, View &owner) : Fl_Widget(x, y, w, h), owner_(owner) {
    if (owner.project_ == "wordcraft")
      Fl::add_timeout(.2, word_poll, this);
  }
  ~Canvas() { Fl::remove_timeout(word_poll, this); }
  void raster(const Json &view) {
    if (owner_.project_ == "wordcraft") {
      word_raster(view);
      return;
    }
    auto value = view.value("raster", Json::object()).value("png", std::string());
    if (value.empty() || value == encoded_)
      return;
    auto bytes = artcraft::decode_base64(value, 16 * 1024 * 1024);
    artcraft::png_dimensions(bytes);
    auto image = std::make_unique<Fl_PNG_Image>(nullptr, bytes.data(), int(bytes.size()));
    if (image->fail())
      throw std::runtime_error("Cannot decode the engine page preview");
    raster_ = std::move(image);
    encoded_ = std::move(value);
    if (owner_.presentation_)
      size(owner_.scroll_->w() - Fl::scrollbar_size(), owner_.scroll_->h() - Fl::scrollbar_size());
    else if (owner_.project_ == "deckcraft")
      relayout_deck();
    else if (owner_.project_ != "gridcraft" || view.value("preview_mode", false))
      size(raster_->w() + 24, raster_->h() + 24);
  }
};
// Rulers use the same page transform as painting and pointer hit testing.
class Ruler : public Fl_Widget {
  View &view_;
  bool vertical_;
  std::string drag_;
  double value_ = 0;
  Json section() const {
    auto view = view_.snapshot_.value("view", Json::object());
    int page = view.value("raster", Json::object()).value("page", 0);
    auto pages = view.value("layout", Json::object()).value("pages", Json::array());
    int index = page >= 0 && page < int(pages.size()) ? pages[page].value("section", 0) : 0;
    auto sections =
        view_.snapshot_.value("document", Json::object()).value("sections", Json::array());
    return index >= 0 && index < int(sections.size())
               ? sections[index].value("props", Json::object())
               : Json::object();
  }
  const PageRect *page() const {
    auto &g = view_.canvas_->geometry();
    int index = view_.visible_page_;
    return index >= 0 && index < int(g.pages.size()) ? &g.pages[index] : nullptr;
  }
  double origin(const PageRect &p) const {
    return vertical_ ? view_.canvas_->y() + p.y : view_.canvas_->x() + p.x;
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x(), y(), w(), h());
    auto *p = page();
    if (!p) {
      fl_pop_clip();
      return;
    }
    auto sec = section();
    double scale = view_.canvas_->geometry().scale, start = origin(*p),
           length = vertical_ ? p->h : p->w;
    double first = sec.value(vertical_ ? "marginTop" : "marginLeft", 72.),
           last = sec.value(vertical_ ? "marginBottom" : "marginRight", 72.);
    fl_color(fl_darker(FL_BACKGROUND_COLOR));
    if (vertical_)
      fl_rectf(x() + 4, int(start), w() - 8, int(length));
    else
      fl_rectf(int(start), y() + 4, int(length), h() - 8);
    fl_color(FL_BACKGROUND2_COLOR);
    if (vertical_)
      fl_rectf(x() + 4, int(start + first * scale), w() - 8,
               std::max(1, int(length - (first + last) * scale)));
    else
      fl_rectf(int(start + first * scale), y() + 4,
               std::max(1, int(length - (first + last) * scale)), h() - 8);
    fl_color(FL_FOREGROUND_COLOR);
    fl_font(labelfont(), labelsize());
    for (int unit = -int(std::ceil(first / 36)); unit < 600; ++unit) {
      double distance = first + unit * 36;
      if (distance * scale > length)
        break;
      if (distance < 0)
        continue;
      int at = int(start + distance * scale);
      bool major = unit % 2 == 0;
      if (vertical_) {
        if (at < y() || at > y() + h())
          continue;
        fl_line(x() + w() - 8, at, x() + w() - (major ? 1 : 4), at);
        if (major) {
          auto text = std::to_string(unit / 2);
          fl_draw(text.c_str(), x() + 3, at + labelsize() / 2);
        }
      } else {
        if (at < x() || at > x() + w())
          continue;
        fl_line(at, y() + h() - 7, at, y() + h() - (major ? 1 : 4));
        if (major) {
          auto text = std::to_string(unit / 2);
          fl_draw(text.c_str(), at + 3, y() + labelsize() + 2);
        }
      }
    }
    if (!vertical_) {
      auto para = view_.snapshot_.value("view", Json::object())
                      .value("paragraph", Json::object())
                      .value("resolved", Json::object());
      double left = first + para.value("indentLeft", 0.),
             right = length / scale - last - para.value("indentRight", 0.),
             initial = left + para.value("indentFirst", 0.);
      for (auto [value, top] :
           {std::pair{left, false}, std::pair{right, false}, std::pair{initial, true}}) {
        int at = int(start + value * scale), cy = top ? y() + 2 : y() + h() - 3;
        fl_color(FL_SELECTION_COLOR);
        fl_polygon(at - 4, cy, at + 4, cy, at, cy + (top ? 6 : -6));
      }
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    auto *p = page();
    if (!p || view_.busy_)
      return 0;
    auto sec = section();
    double scale = view_.canvas_->geometry().scale, length = (vertical_ ? p->h : p->w) / scale,
           at = ((vertical_ ? Fl::event_y() : Fl::event_x()) - origin(*p)) / scale;
    if (event == FL_PUSH) {
      double first = sec.value(vertical_ ? "marginTop" : "marginLeft", 72.),
             last = length - sec.value(vertical_ ? "marginBottom" : "marginRight", 72.);
      drag_.clear();
      if (vertical_) {
        if (std::abs(at - first) * scale < 10)
          drag_ = "top";
        else if (std::abs(at - last) * scale < 10)
          drag_ = "bottom";
      } else {
        auto para = view_.snapshot_.value("view", Json::object())
                        .value("paragraph", Json::object())
                        .value("resolved", Json::object());
        double left = first + para.value("indentLeft", 0.),
               right = last - para.value("indentRight", 0.),
               initial = left + para.value("indentFirst", 0.);
        if (Fl::event_y() < y() + h() / 2 && std::abs(at - initial) * scale < 10)
          drag_ = "firstLine";
        else if (std::abs(at - left) * scale < 10)
          drag_ = "left";
        else if (std::abs(at - right) * scale < 10)
          drag_ = "right";
      }
      return !drag_.empty();
    }
    if ((event == FL_DRAG || event == FL_RELEASE) && !drag_.empty()) {
      value_ = std::clamp(std::round(at * 2) / 2, 0., length - 18.);
      if (event == FL_RELEASE) {
        auto drag = std::exchange(drag_, {});
        if (vertical_)
          view_.submit_({{"layout.margins", {{drag, drag == "top" ? value_ : length - value_}}}});
        else {
          auto para = view_.snapshot_.value("view", Json::object())
                          .value("paragraph", Json::object())
                          .value("resolved", Json::object());
          double first = sec.value("marginLeft", 72.), last = sec.value("marginRight", 72.);
          double value = drag == "right" ? length - last - value_ : value_ - first;
          if (drag == "firstLine")
            value -= para.value("indentLeft", 0.);
          view_.submit_({{"para.indents", {{drag, value}}}});
        }
      }
      return 1;
    }
    return 0;
  }

public:
  Ruler(int x, int y, int w, int h, View &view, bool vertical)
      : Fl_Widget(x, y, w, h), view_(view), vertical_(vertical) {
    tooltip(vertical ? "Page margins: drag the top or bottom boundary (inches)"
                     : "Paragraph ruler: drag first-line, left or right indent (inches)");
  }
};
class Input : public Fl_Multiline_Input {
  View &owner_;
  int handle(int event) override {
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)) {
      if (Fl::event_state(FL_CTRL)) {
        owner_.commit();
        return 1;
      }
      if (owner_.project_ == "gridcraft" && !Fl::event_state(FL_ALT | FL_SHIFT)) {
        owner_.commit();
        owner_.submit_({{"selection.next", Json::object()}});
        return 1;
      }
    }
    return Fl_Multiline_Input::handle(event);
  }

public:
  Input(int x, int y, int w, int h, const char *label, View &owner)
      : Fl_Multiline_Input(x, y, w, h, label), owner_(owner) {}
};
View::View(int x, int y, int w, int h, std::string project, Submit submit,
           std::function<void(const std::string &)> invoke)
    : Fl_Group(x, y, w, h, "Document"), invoke_(std::move(invoke)), project_(std::move(project)),
      submit_(std::move(submit)) {
  navigator_ = new Fl_Hold_Browser(x + 8, y + 12, 180, h - 24);
  navigator_->format_char(0);
  navigator_->callback([](Fl_Widget *, void *p) { static_cast<View *>(p)->navigate(); }, this);
  address_ = new Fl_Input(x + 280, y + 12, w - 300, 28);
  address_->tooltip(project_ == "gridcraft" ? "Cell address: type a reference and press Enter"
                                            : "Selected paragraph or slide shape");
  address_->when(FL_WHEN_ENTER_KEY_ALWAYS);
  address_->callback(
      [](Fl_Widget *, void *p) {
        auto &v = *static_cast<View *>(p);
        if (v.project_ == "gridcraft" && !v.busy_ && !v.pending_)
          v.submit_({{"selection.set", {{"range", v.address_->value()}}}});
      },
      this);
  if (project_ == "deckcraft")
    address_->readonly(1);
  if (project_ == "wordcraft") {
    address_->tooltip("Search document: enter text and press Enter");
    address_->callback(
        [](Fl_Widget *, void *p) {
          auto &v = *static_cast<View *>(p);
          if (!v.busy_ && !v.pending_)
            v.submit_({{"edit.findNext", {{"text", v.address_->value()}}}});
        },
        this);
  }
  scroll_ = new Fl_Scroll(x + 200, y + 50, w - 212, h - 230);
  canvas_ = new Canvas(x + 200, y + 50, w - 232, h - 250, *this);
  canvas_->copy_label("Document page surface");
  scroll_->end();
  if (project_ == "gridcraft") {
    grid_ = new Grid(x + 200, y + 50, w - 212, h - 246, *this);
    scroll_->hide();
  }
  editor_ = new Input(x + 200, y + h - 166, w - 212, 108, "Paragraph / shape text or cell formula",
                      *this);
  editor_->align(FL_ALIGN_TOP_LEFT);
  editor_->when(FL_WHEN_CHANGED);
  editor_->callback([](Fl_Widget *, void *p) { static_cast<View *>(p)->pending_ = true; }, this);
  apply_ = new Fl_Button(x + 200, y + h - 46, 170, 30, "Apply edit");
  apply_->callback([](Fl_Widget *, void *p) { static_cast<View *>(p)->apply(); }, this);
  office_ui_ = std::make_unique<OfficeUi>(*this);
  if (project_ == "wordcraft") {
    horizontal_ = new Ruler(x, y, w, 24, *this, false);
    vertical_ = new Ruler(x, y, 24, h, *this, true);
  }
  if (project_ == "deckcraft")
    slide_list_ = new SlideList(x, y, 168, 200, *this);
  if (project_ == "gridcraft") {
    sheet_tabs_ = new Fl_Tabs(x, y, w - 40, 32);
    sheet_tabs_->end();
    sheet_tabs_->callback(
        [](Fl_Widget *, void *p) {
          auto &v = *static_cast<View *>(p);
          auto *selected = v.sheet_tabs_->value();
          if (selected && !v.busy_)
            v.submit_({{"sheet.activate",
                        {{"sheet", int(reinterpret_cast<intptr_t>(selected->user_data()) - 1)}}}});
        },
        this);
    add_sheet_ = new Fl_Button(x, y, 32, 28, "+");
    add_sheet_->tooltip("New sheet");
    add_sheet_->callback(
        [](Fl_Widget *, void *p) {
          auto &v = *static_cast<View *>(p);
          if (!v.busy_)
            v.submit_({{"home.insertSheet", Json::object()}});
        },
        this);
  }
  editor_->hide();
  apply_->hide();
  navigator_->hide();
  address_->hide();
  resizable(nullptr);
  layout_editor();
  end();
}
void View::pick(int index) {
  if (pending_ || busy_)
    return; // Keep uncommitted text until Apply or explicit discard.
  const auto doc = snapshot_.value("document", Json::object());
  if (project_ == "gridcraft") {
    row_ = index / 16;
    col_ = index % 16;
    address_->value(cell(row_, col_).c_str());
    auto values = value_rows(snapshot_.value("view", Json::object()), "inputs");
    editor_->value(row_ < int(values.size()) && col_ < int(values[row_].size())
                       ? text(values[row_][col_]).c_str()
                       : "");
  } else if (project_ == "wordcraft") {
    for (const auto &b : doc.value("blocks", Json::array()))
      if (b.value("index", -1) == index && b.value("type", "") == "paragraph") {
        block_ = index;
        editor_->value(b.value("text", std::string()).c_str());
        if (!office_ui_)
          address_->value(("Paragraph " + std::to_string(index + 1)).c_str());
        break;
      }
  } else {
    auto shapes = snapshot_.value("view", Json::object())
                      .value("slide", Json::object())
                      .value("shapes", Json::array());
    if (index >= 0 && index < int(shapes.size())) {
      if (shape_id_ != shapes[index].at("id"))
        deck_editing_ = false;
      shape_id_ = shapes[index].at("id");
      editor_->value(shapes[index].value("text", std::string()).c_str());
      address_->value(("Shape " + shape_id_.dump()).c_str());
    }
  }
  canvas_->redraw();
}
void View::navigate() {
  if (busy_ || pending_)
    return;
  int selected = navigator_->value() - 1;
  if (selected < 0)
    return;
  if (project_ == "wordcraft") {
    if (selected < int(navigation_blocks_.size()))
      submit_({{"caret.set",
                {{"pos",
                  {{"path", Json::array({navigation_blocks_[selected]})},
                   {"off", 0},
                   {"story", "body"}}}}}});
  } else if (project_ == "gridcraft")
    submit_({{"sheet.activate", {{"sheet", selected}}}});
  else
    submit_({{"slide.go", {{"index", selected}}}});
}
void View::apply() {
  if (busy_ || !pending_)
    return;
  std::string value = editor_->value();
  if (project_ == "wordcraft" && block_ >= 0) {
    for (const auto &b : snapshot_.at("document").at("blocks"))
      if (b.value("index", -1) == block_) {
        Json anchor = {{"path", Json::array({block_})}, {"off", 0}, {"story", "body"}},
             focus = anchor;
        focus["off"] = b.at("len");
        submit_({{"select.range", {{"anchor", anchor}, {"focus", focus}}},
                 {"text.insert", {{"text", value}, {"paragraphs", true}}}});
        break;
      }
  } else if (project_ == "gridcraft")
    submit_({{"cell.set", {{"cell", address_->value()}, {"input", value}}}});
  else if (!shape_id_.is_null())
    submit_({{"text.set", {{"id", shape_id_}, {"text", value}}}});
  // Clear only after a successful engine reply (update); failed commands keep text.
}
void View::update(const Json &snapshot, bool busy) {
  snapshot_ = snapshot;
  busy_ = busy;
  if (office_ui_) {
    office_ui_->update(snapshot, busy);
    layout_editor();
  }
  if (busy) {
    navigator_->deactivate();
    editor_->deactivate();
    apply_->deactivate();
    return;
  }
  navigator_->activate();
  editor_->activate();
  apply_->activate();
  auto view = snapshot_.value("view", Json::object());
  if (grid_) {
    if (view.value("preview_mode", false)) {
      grid_->hide();
      scroll_->show();
    } else {
      grid_->show();
      scroll_->hide();
      grid_->refresh(snapshot_);
    }
  }
  canvas_->raster(view);
  const auto doc = snapshot_.value("document", Json::object());
  int previous = navigator_->value();
  navigator_->clear();
  auto entries = doc.value(project_ == "wordcraft"   ? "blocks"
                           : project_ == "gridcraft" ? "sheets"
                                                     : "slides",
                           Json::array());
  navigation_blocks_.clear();
  for (const auto &entry : entries) {
    if (project_ == "wordcraft") {
      auto style = entry.value("style", std::string());
      if (!style.starts_with("Heading") && style != "Title")
        continue;
      navigation_blocks_.push_back(entry.value("index", 0));
    }
    auto value = project_ == "wordcraft"
                     ? entry.value("text", std::string("[object]"))
                     : entry.value(project_ == "gridcraft" ? "name" : "title", std::string());
    std::replace(value.begin(), value.end(), '\n', ' ');
    value = " " + value.substr(0, 100);
    navigator_->add(value.c_str());
  }
  if (project_ == "deckcraft")
    previous = doc.value("selection", Json::object()).value("slide", 0) + 1;
  if (project_ == "gridcraft")
    previous = doc.value("activeSheet", 0) + 1;
  navigator_->value(std::max(1, previous));
  if (slide_list_)
    slide_list_->refresh(snapshot_);
  if (sheet_tabs_) {
    auto sheets = doc.value("sheets", Json::array());
    std::string names;
    for (const auto &sheet : sheets)
      names += sheet.value("name", std::string()) + "\n";
    if (names != sheet_names_) {
      sheet_names_ = names;
      auto *parent = Fl_Group::current();
      sheet_tabs_->clear();
      sheet_tabs_->begin();
      for (size_t i = 0; i < sheets.size(); ++i) {
        auto *group = new Fl_Group(sheet_tabs_->x(), sheet_tabs_->y() + 30, sheet_tabs_->w(), 1);
        group->copy_label(sheets[i].value("name", std::string()).c_str());
        group->user_data(reinterpret_cast<void *>(i + 1));
        group->end();
      }
      sheet_tabs_->end();
      Fl_Group::current(parent);
      if (invoke_)
        invoke_("@appearance");
    }
    int active = doc.value("activeSheet", 0);
    if (active >= 0 && active < sheet_tabs_->children())
      sheet_tabs_->value(sheet_tabs_->child(active));
  }
  if (!pending_) {
    if (project_ == "wordcraft") {
      auto focus = doc.value("selection", Json::object())
                       .value("focus", Json::object())
                       .value("path", Json::array());
      if (!focus.empty() && focus[0].is_number_integer())
        block_ = focus[0];
      pick(block_ < 0 ? 0 : block_);
    } else if (project_ == "gridcraft") {
      auto active = doc.value("activeCell", std::string("A1"));
      address_->value(active.c_str());
      // Coordinate input can target all engine-supported cells; preview is A1:Hn.
      auto input = snapshot_.value("view", Json::object()).value("active_input", Json::object());
      editor_->value(input.value("input", std::string()).c_str());
    } else {
      auto shapes = snapshot_.value("view", Json::object())
                        .value("slide", Json::object())
                        .value("shapes", Json::array());
      auto selection = doc.value("selection", Json::object());
      auto selected = selection.value("shapes", Json::array());
      if (!selected.empty())
        shape_id_ = selected.at(0);
      if (selection.contains("text") && !selection.at("text").is_null())
        deck_editing_ = true;
      int index = 0;
      for (int i = 0; i < int(shapes.size()); ++i)
        if (shapes[i].value("id", Json()) == shape_id_) {
          index = i;
          break;
        }
      pick(index);
    }
  }
  if (project_ != "wordcraft")
    layout_editor();
  if (office_ui_ && find_focus_) {
    find_focus_ = false;
    address_->take_focus();
  }
  redraw();
}
} // namespace dome_office

dome_office::View::~View() = default;

void dome_office::View::presentation(bool value) {
  presentation_ = value;
  if (office_ui_)
    office_ui_->visible(!value);
  if (slide_list_) {
    if (value)
      slide_list_->hide();
    else
      slide_list_->show();
  }
  if (value) {
    navigator_->hide();
    address_->hide();
    editor_->hide();
    apply_->hide();
    scroll_->resize(x() + 8, y() + 8, w() - 16, h() - 16);
    canvas_->resize(x() + 8, y() + 8, scroll_->w() - Fl::scrollbar_size(),
                    scroll_->h() - Fl::scrollbar_size());
    Fl::focus(canvas_);
  } else {
    navigator_->show();
    address_->show();
    editor_->show();
    apply_->show();
    scroll_->resize(x() + 200, y() + 50, w() - 212, h() - 230);
    canvas_->position(scroll_->x(), scroll_->y());
  }
  if (!value)
    layout_editor();
  redraw();
}

void dome_office::View::paste() {
  if (grid_)
    Fl::paste(*grid_, 1);
  else
    Fl::paste(*canvas_, 1);
}

void dome_office::View::layout_editor() {
  if (!office_ui_)
    return;
  office_ui_->layout();
  if (project_ == "gridcraft") {
    int top = y() + office_ui_->top(), bottom = y() + h() - office_ui_->bottom() - 34;
    navigator_->hide();
    address_->show();
    editor_->show();
    apply_->show();
    editor_->copy_label("");
    address_->resize(x() + 8, top + 6, 106, 28);
    editor_->resize(x() + 164, top + 6, std::max(80, w() - 258), 28);
    apply_->copy_label("Apply");
    apply_->resize(x() + w() - 86, top + 6, 78, 28);
    grid_->resize(x() + 4, top + 42, w() - 8, std::max(100, bottom - top - 44));
    scroll_->resize(grid_->x(), grid_->y(), grid_->w(), grid_->h());
    sheet_tabs_->resize(x() + 8, bottom, w() - 52, 32);
    for (int i = 0; i < sheet_tabs_->children(); ++i)
      sheet_tabs_->child(i)->resize(sheet_tabs_->x(), bottom + 30, sheet_tabs_->w(), 1);
    add_sheet_->resize(x() + w() - 38, bottom + 2, 30, 26);
    auto doc = snapshot_.value("document", Json::object());
    auto sheets = doc.value("sheets", Json::array());
    int sheet = doc.value("activeSheet", 0);
    double z =
        sheet >= 0 && sheet < int(sheets.size()) ? sheets[sheet].value("zoom", 100) / 100. : 1.;
    grid_->zoom(z);
    office_ui_->geometry(0, z * 4 / 3.);
    return;
  }
  if (project_ == "deckcraft") {
    if (presentation_)
      return;
    int top = y() + office_ui_->top(), bottom = y() + h() - office_ui_->bottom();
    navigator_->hide();
    slide_list_->show();
    slide_list_->resize(x() + 8, top + 8, 168, std::max(100, bottom - top - 16));
    address_->hide();
    if (paragraph_editor_) {
      editor_->show();
      apply_->show();
      editor_->resize(x() + 192, bottom - 124, w() - 208, 80);
      apply_->resize(x() + 192, bottom - 36, 170, 28);
      bottom -= 146;
    } else {
      editor_->hide();
      apply_->hide();
    }
    scroll_->resize(x() + 184, top + 8, std::max(100, w() - 192), std::max(100, bottom - top - 16));
    canvas_->position(scroll_->x() - scroll_->xposition(), scroll_->y() - scroll_->yposition());
    canvas_->relayout_deck();
    office_ui_->geometry(0, canvas_->deck_scale() * 4 / 3.);
    return;
  }
  auto settings = snapshot_.value("view", Json::object()).value("settings", Json::object());
  bool nav = settings.value("navPane", false), ruler = settings.value("ruler", true);
  int top = y() + office_ui_->top(), bottom = y() + h() - office_ui_->bottom(),
      side = nav ? 208 : 0, edge = ruler ? 24 : 0;
  if (nav) {
    navigator_->show();
    address_->show();
    address_->resize(x() + 8, top + 10, 188, 28);
    navigator_->resize(x() + 8, top + 48, 188, std::max(40, bottom - top - 56));
  } else {
    navigator_->hide();
    address_->hide();
  }
  if (paragraph_editor_) {
    editor_->show();
    apply_->show();
    editor_->resize(x() + side + 32, bottom - 132, std::max(80, w() - side - 44), 90);
    apply_->resize(x() + side + 32, bottom - 36, 170, 28);
    bottom -= 158;
  } else {
    editor_->hide();
    apply_->hide();
  }
  scroll_->resize(x() + side + edge, top + edge, std::max(80, w() - side - edge),
                  std::max(80, bottom - top - edge));
  canvas_->position(scroll_->x() - scroll_->xposition(), scroll_->y() - scroll_->yposition());
  canvas_->relayout_word();
  office_ui_->geometry(visible_page_, canvas_->geometry().scale);
  if (ruler) {
    horizontal_->show();
    vertical_->show();
    horizontal_->resize(scroll_->x(), top, scroll_->w(), 24);
    vertical_->resize(x() + side, scroll_->y(), 24, scroll_->h());
  } else {
    horizontal_->hide();
    vertical_->hide();
  }
}
void dome_office::View::resize(int x, int y, int w, int h) {
  if (office_ui_) {
    Fl_Widget::resize(x, y, w, h);
    layout_editor();
  } else
    Fl_Group::resize(x, y, w, h);
}
void dome_office::View::paragraph_editor(bool value) {
  paragraph_editor_ = value;
  layout_editor();
  redraw();
}
void dome_office::View::message(const std::string &text) {
  if (office_ui_)
    office_ui_->message(text);
}
void dome_office::View::focus_document() { Fl::focus(canvas_); }
dome_office::Json dome_office::View::ui_state() const {
  Json state = {
      {"native_office_layout", bool(office_ui_)},
      {"native_word_layout", project_ == "wordcraft"},
      {"paragraph_editor", paragraph_editor_},
      {"navigation", bool(navigator_->Fl_Widget::visible())},
      {"canvas",
       {{"x", scroll_->x()}, {"y", scroll_->y()}, {"w", scroll_->w()}, {"h", scroll_->h()}}}};
  if (office_ui_) {
    state["ribbon"] = office_ui_->state();
    const auto &g = canvas_->geometry();
    state["scale"] = g.scale;
    state["pages"] = Json::array();
    for (auto p : g.pages)
      state["pages"].push_back({{"page", p.page}, {"x", p.x}, {"y", p.y}, {"w", p.w}, {"h", p.h}});
  }
  return state;
}

void dome_office::View::find() {
  find_focus_ = true;
  submit_({{"view.navigationPane", {{"value", true}}}});
}
