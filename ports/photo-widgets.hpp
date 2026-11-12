// SPDX-License-Identifier: MIT
#pragma once
#include "gpu-image.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Value_Input.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <artcraft/application.hpp>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace dome_photo {
struct MenuEntry {
  std::string label;
  std::function<void()> action;
  bool enabled = true, checked = false;
};
// An in-window popup: all drawing/input uses the same native GPU surface.
// No nested FLTK menu window or compositor popup grab is required.
class ContextMenu : public Fl_Group {
  inline static std::map<Fl_Window *, ContextMenu *> menus_;
  Fl_Window *owner_ = nullptr;
  Fl_Scroll *scroll_;
  std::vector<MenuEntry> entries_;
  std::vector<Fl_Button *> rows_;
  int selected_ = 0;
  Fl_Widget *previous_ = nullptr;
  void draw() override {
    dome_office::GpuClip clip(*this, {x(), y(), w(), h()});
    Fl_Group::draw();
  }
  static void clicked(Fl_Widget *widget, void *data) {
    auto &self = *static_cast<ContextMenu *>(widget->parent()->parent());
    self.choose(int(reinterpret_cast<intptr_t>(data)));
  }

public:
  ContextMenu() : Fl_Group(0, 0, 1, 1) {
    owner_ = window();
    menus_[owner_] = this;
    box(FL_BORDER_BOX);
    begin();
    scroll_ = new Fl_Scroll(0, 0, 1, 1);
    scroll_->end();
    end();
    hide();
  }
  ~ContextMenu() {
    if (menus_.contains(owner_) && menus_[owner_] == this)
      menus_.erase(owner_);
  }
  static ContextMenu *existing(Fl_Window *window) {
    auto found = menus_.find(window);
    return found == menus_.end() ? nullptr : found->second;
  }
  static ContextMenu *get(Fl_Window *window) {
    if (auto *menu = existing(window))
      return menu;
    auto *parent = Fl_Group::current();
    window->begin();
    auto *menu = new ContextMenu();
    window->end();
    Fl_Group::current(parent);
    return menu;
  }
  const auto &entries() const { return entries_; }
  const auto &rows() const { return rows_; }
  void close() {
    hide();
    if (previous_ && previous_->visible_r())
      Fl::focus(previous_);
    window()->redraw();
  }
  void choose(int index) {
    if (index < 0 || index >= int(entries_.size()) || !entries_[index].enabled)
      return;
    auto action = entries_[index].action;
    close();
    if (action)
      action();
  }
  bool contains(int X, int Y) const {
    return visible() && X >= x() && X < x() + w() && Y >= y() && Y < y() + h();
  }
  void open(int X, int Y, std::vector<MenuEntry> entries, int font, int pixels) {
    previous_ = Fl::focus();
    labelfont(font);
    labelsize(pixels);
    scroll_->clear();
    rows_.clear();
    entries_ = std::move(entries);
    selected_ = 0;
    fl_font(font, pixels);
    int width = 180, rowheight = std::max(30, pixels + 12);
    for (auto &item : entries_)
      width = std::max(width, int(std::ceil(fl_width(item.label.c_str()))) + 52);
    width = std::min(width, window()->w() - 8);
    int height = std::min(int(entries_.size()) * rowheight + 8, window()->h() - 12);
    X = std::clamp(X, 4, std::max(4, window()->w() - width - 4));
    Y = std::clamp(Y, 4, std::max(4, window()->h() - height - 4));
    resize(X, Y, width, height);
    scroll_->resize(X + 3, Y + 3, width - 6, height - 6);
    scroll_->scroll_to(0, 0);
    scroll_->begin();
    for (int i = 0; i < int(entries_.size()); ++i) {
      auto *row = new Fl_Button(X + 5, Y + 5 + i * rowheight, width - 24, rowheight);
      row->copy_label(((entries_[i].checked ? "* " : "  ") + entries_[i].label).c_str());
      row->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
      row->box(FL_FLAT_BOX);
      row->labelfont(font);
      row->labelsize(pixels);
      row->callback(clicked, reinterpret_cast<void *>(intptr_t(i)));
      if (!entries_[i].enabled)
        row->deactivate();
      rows_.push_back(row);
    }
    scroll_->end();
    show();
    take_focus();
    window()->redraw();
  }
  int handle(int event) override {
    if (event == FL_FOCUS || event == FL_UNFOCUS)
      return 1;
    if (event == FL_MOVE) {
      for (size_t i = 0; i < rows_.size(); ++i)
        if (entries_[i].enabled && Fl::event_y() >= rows_[i]->y() &&
            Fl::event_y() < rows_[i]->y() + rows_[i]->h())
          selected_ = i;
      for (size_t i = 0; i < rows_.size(); ++i)
        rows_[i]->color(i == size_t(selected_) ? FL_SELECTION_COLOR : FL_BACKGROUND_COLOR);
      redraw();
    }
    if (event == FL_KEYDOWN) {
      if (Fl::event_key() == FL_Escape) {
        close();
        return 1;
      }
      if (Fl::event_key() == FL_Enter || Fl::event_key() == ' ') {
        choose(selected_);
        return 1;
      }
      if (Fl::event_key() == FL_Up || Fl::event_key() == FL_Down) {
        int delta = Fl::event_key() == FL_Up ? -1 : 1;
        for (size_t i = 0; i < entries_.size(); ++i) {
          selected_ = (selected_ + delta + entries_.size()) % entries_.size();
          if (entries_[selected_].enabled)
            break;
        }
        for (size_t i = 0; i < rows_.size(); ++i)
          rows_[i]->color(i == size_t(selected_) ? FL_SELECTION_COLOR : FL_BACKGROUND_COLOR);
        scroll_->scroll_to(0, std::max(0, selected_ * std::max(30, labelsize() + 12) - h() / 2));
        redraw();
        return 1;
      }
    }
    return Fl_Group::handle(event);
  }
};
class NativeChoice : public Fl_Choice {
  bool pressed_ = false;
  void popup() {
    std::vector<MenuEntry> entries;
    for (int index = 0; index < size() - 1; ++index) {
      if (!text(index))
        continue;
      entries.push_back({text(index),
                         [this, index] {
                           value(index);
                           redraw();
                           do_callback();
                         },
                         !(mode(index) & FL_MENU_INACTIVE), value() == index});
    }
    if (!entries.empty())
      ContextMenu::get(window())->open(x(), y() + h(), std::move(entries), textfont(), textsize());
  }

public:
  using Fl_Choice::Fl_Choice;
  int handle(int event) override {
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
      pressed_ = true;
      take_focus();
      return 1;
    }
    if (event == FL_RELEASE && pressed_) {
      pressed_ = false;
      if (Fl::event_x() >= x() && Fl::event_x() < x() + w() && Fl::event_y() >= y() &&
          Fl::event_y() < y() + h())
        popup();
      return 1;
    }
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Enter || Fl::event_key() == ' ')) {
      popup();
      return 1;
    }
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Down || Fl::event_key() == FL_Up)) {
      int direction = Fl::event_key() == FL_Down ? 1 : -1, count = size() - 1;
      for (int i = 1; i <= count; ++i) {
        int index = (value() + i * direction + count * 2) % count;
        if (!(mode(index) & FL_MENU_INACTIVE)) {
          value(index);
          redraw();
          do_callback();
          break;
        }
      }
      return 1;
    }
    if (event == FL_FOCUS || event == FL_UNFOCUS)
      return 1;
    return Fl_Widget::handle(event);
  }
};
// Photoshop-style saturation/value plane. Draw directly in the native GPU
// surface instead of FLTK's software image callback used by Fl_Color_Chooser.
class ColorPicker : public Fl_Group {
  double hue_ = 0, saturation_ = 0, value_ = 0;
  double red_ = 0, green_ = 0, blue_ = 0;
  Fl_Value_Input *components_[3];
  Fl_Input *hex_;
  class Plane : public Fl_Widget {
    ColorPicker &owner_;
    bool hue_strip_;
    void draw() override {
      dome_office::GpuClip clip(*this, {x(), y(), w(), h()});
      glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);
      glDisable(GL_TEXTURE_2D);
      glDisable(GL_BLEND);
      glBegin(GL_QUADS);
      const int columns = hue_strip_ ? 1 : 64, rows = 128;
      for (int row = 0; row < rows; ++row)
        for (int col = 0; col < columns; ++col) {
          double r, g, b;
          Fl_Color_Chooser::hsv2rgb(hue_strip_ ? 6. * row / (rows - 1) : owner_.hue_,
                                    hue_strip_ ? 1 : double(col) / (columns - 1),
                                    hue_strip_ ? 1 : 1. - double(row) / (rows - 1), r, g, b);
          glColor3d(r, g, b);
          int left = x() + col * w() / columns, right = x() + (col + 1) * w() / columns;
          int top = y() + row * h() / rows, bottom = y() + (row + 1) * h() / rows;
          glVertex2i(left, top);
          glVertex2i(right, top);
          glVertex2i(right, bottom);
          glVertex2i(left, bottom);
        }
      glEnd();
      glPopAttrib();
      int px = x() + int(owner_.saturation_ * (w() - 1)),
          py = y() + int((1 - owner_.value_) * (h() - 1));
      if (hue_strip_) {
        py = y() + int(owner_.hue_ / 6 * (h() - 1));
        fl_color(FL_BLACK);
        fl_rect(x(), py - 2, w(), 5);
        fl_color(FL_WHITE);
        fl_rect(x() + 1, py - 1, w() - 2, 3);
      } else {
        fl_color(FL_BLACK);
        fl_rect(px - 4, py - 4, 9, 9);
        fl_color(FL_WHITE);
        fl_rect(px - 3, py - 3, 7, 7);
      }
    }

  public:
    Plane(int X, int Y, int W, int H, ColorPicker &owner, bool hue)
        : Fl_Widget(X, Y, W, H), owner_(owner), hue_strip_(hue) {
      tooltip(hue ? "Hue; use Up/Down to adjust"
                  : "Saturation and brightness; use arrow keys to adjust");
    }
    int handle(int event) override {
      if (event == FL_FOCUS || event == FL_UNFOCUS)
        return 1;
      if (event == FL_PUSH || event == FL_DRAG) {
        if (event == FL_PUSH && Fl::event_button() != FL_LEFT_MOUSE)
          return 0;
        take_focus();
        if (hue_strip_)
          owner_.hue_ =
              5.999999 * std::clamp(double(Fl::event_y() - y()) / std::max(1, h() - 1), 0., 1.);
        else {
          owner_.saturation_ =
              std::clamp(double(Fl::event_x() - x()) / std::max(1, w() - 1), 0., 1.);
          owner_.value_ =
              1 - std::clamp(double(Fl::event_y() - y()) / std::max(1, h() - 1), 0., 1.);
        }
        owner_.changed_hsv();
        return 1;
      }
      if (event == FL_RELEASE)
        return 1;
      if (event == FL_KEYDOWN) {
        int key = Fl::event_key();
        if (key != FL_Left && key != FL_Right && key != FL_Up && key != FL_Down)
          return 0;
        if (hue_strip_)
          owner_.hue_ =
              std::clamp(owner_.hue_ + (key == FL_Up || key == FL_Left ? -.02 : .02), 0., 5.999999);
        else {
          if (key == FL_Left || key == FL_Right)
            owner_.saturation_ =
                std::clamp(owner_.saturation_ + (key == FL_Left ? -.01 : .01), 0., 1.);
          else
            owner_.value_ = std::clamp(owner_.value_ + (key == FL_Down ? -.01 : .01), 0., 1.);
        }
        owner_.changed_hsv();
        return 1;
      }
      return Fl_Widget::handle(event);
    }
  };
  Plane *square_, *strip_;
  Fl_Box *swatch_;
  void sync() {
    components_[0]->value(std::round(red_ * 255));
    components_[1]->value(std::round(green_ * 255));
    components_[2]->value(std::round(blue_ * 255));
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", int(std::round(red_ * 255)),
                  int(std::round(green_ * 255)), int(std::round(blue_ * 255)));
    hex_->value(hex);
    swatch_->color(fl_rgb_color(uchar(std::round(red_ * 255)), uchar(std::round(green_ * 255)),
                                uchar(std::round(blue_ * 255))));
    redraw();
  }
  void changed_hsv() {
    Fl_Color_Chooser::hsv2rgb(hue_, saturation_, value_, red_, green_, blue_);
    sync();
  }

public:
  ColorPicker(int X, int Y, int W, int H) : Fl_Group(X, Y, W, H) {
    square_ = new Plane(X, Y, 340, H, *this, false);
    strip_ = new Plane(X + 358, Y, 32, H, *this, true);
    swatch_ = new Fl_Box(X + 472, Y, 204, 36);
    swatch_->box(FL_FLAT_BOX);
    const char *labels[] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i) {
      components_[i] = new Fl_Value_Input(X + 472, Y + 48 + i * 44, 204, 34, labels[i]);
      components_[i]->range(0, 255);
      components_[i]->step(1);
      components_[i]->callback(
          [](Fl_Widget *, void *data) {
            auto &self = *static_cast<ColorPicker *>(data);
            self.rgb(self.components_[0]->value() / 255, self.components_[1]->value() / 255,
                     self.components_[2]->value() / 255);
          },
          this);
    }
    hex_ = new Fl_Input(X + 472, Y + 184, 204, 34, "Hex");
    hex_->maximum_size(7);
    hex_->when(FL_WHEN_CHANGED);
    hex_->callback(
        [](Fl_Widget *, void *data) {
          auto &self = *static_cast<ColorPicker *>(data);
          std::string value = self.hex_->value();
          if (value.size() != 7 || value[0] != '#' ||
              value.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            return;
          unsigned rgb = unsigned(std::stoul(value.substr(1), nullptr, 16));
          self.rgb((rgb >> 16) / 255., ((rgb >> 8) & 255) / 255., (rgb & 255) / 255.);
        },
        this);
    end();
    rgb(0, 0, 0);
  }
  void rgb(double r, double g, double b) {
    red_ = std::clamp(r, 0., 1.);
    green_ = std::clamp(g, 0., 1.);
    blue_ = std::clamp(b, 0., 1.);
    Fl_Color_Chooser::rgb2hsv(red_, green_, blue_, hue_, saturation_, value_);
    sync();
  }
  double r() const { return red_; }
  double g() const { return green_; }
  double b() const { return blue_; }
};
// Overflow tabs use the same in-window popup as other PhotoCraft controls.
class NativeTabs : public Fl_Tabs {
  bool pressed_ = false;
  void popup() {
    std::vector<MenuEntry> entries;
    for (int i = 0; i < children(); ++i) {
      std::string label = child(i)->label() ? child(i)->label() : "Untitled";
      entries.push_back({label,
                         [this, i, label] {
                           // Document cards can be rebuilt while an engine operation finishes.
                           if (i < children() && child(i)->label() && label == child(i)->label()) {
                             value(child(i));
                             redraw();
                             do_callback();
                           }
                         },
                         true, child(i) == value()});
    }
    ContextMenu::get(window())->open(x(), y() + std::abs(tab_height()), std::move(entries),
                                     labelfont(), labelsize());
  }

public:
  using Fl_Tabs::Fl_Tabs;
  int handle(int event) override {
    const int header = std::abs(tab_height());
    bool in_header = Fl::event_y() >= y() && Fl::event_y() < y() + header;
    if (event == FL_PUSH && in_header &&
        (Fl::event_button() == FL_RIGHT_MOUSE || Fl::event_x() >= x() + w() - 30)) {
      pressed_ = true;
      Fl_Widget::take_focus();
      return 1;
    }
    if (event == FL_RELEASE && pressed_) {
      pressed_ = false;
      popup();
      return 1;
    }
    return Fl_Tabs::handle(event);
  }
};
class DialogBody : public Fl_Group {
public:
  using Fl_Group::Fl_Group;
  int handle(int event) override {
    auto *menu = ContextMenu::existing(window());
    if (menu && menu->visible() && event == FL_PUSH &&
        !menu->contains(Fl::event_x(), Fl::event_y())) {
      menu->close();
      return 1;
    }
    return Fl_Group::handle(event);
  }
};
class CurveEditor : public Fl_Widget {
  std::vector<std::pair<int, int>> points_{{0, 0}, {255, 255}};
  int selected_ = -1;
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND2_COLOR);
    fl_rectf(x(), y(), w(), h());
    fl_color(FL_DARK3);
    for (int i = 0; i <= 4; ++i) {
      fl_line(x() + i * w() / 4, y(), x() + i * w() / 4, y() + h());
      fl_line(x(), y() + i * h() / 4, x() + w(), y() + i * h() / 4);
    }
    fl_color(FL_FOREGROUND_COLOR);
    for (size_t i = 1; i < points_.size(); ++i)
      fl_line(x() + points_[i - 1].first * w() / 255, y() + h() - points_[i - 1].second * h() / 255,
              x() + points_[i].first * w() / 255, y() + h() - points_[i].second * h() / 255);
    for (auto point : points_)
      fl_rectf(x() + point.first * w() / 255 - 3, y() + h() - point.second * h() / 255 - 3, 6, 6);
    fl_pop_clip();
  }

public:
  CurveEditor(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {
    tooltip("Click to add a curve point; drag to adjust it. Delete removes an interior point.");
  }
  const auto &points() const { return points_; }
  int handle(int event) override {
    if (event == FL_FOCUS || event == FL_UNFOCUS)
      return 1;
    int px = std::clamp((Fl::event_x() - x()) * 255 / std::max(1, w()), 0, 255),
        py = std::clamp((y() + h() - Fl::event_y()) * 255 / std::max(1, h()), 0, 255);
    if (event == FL_PUSH) {
      take_focus();
      selected_ = -1;
      int best = 100;
      for (size_t i = 0; i < points_.size(); ++i) {
        int d = std::abs(points_[i].first - px) + std::abs(points_[i].second - py);
        if (d < best) {
          best = d;
          selected_ = i;
        }
      }
      auto same = std::find_if(points_.begin(), points_.end(),
                               [&](auto point) { return point.first == px; });
      if (best > 12 && same != points_.end()) {
        selected_ = int(same - points_.begin());
        same->second = py;
      } else if (best > 12 && points_.size() < 19) {
        points_.push_back({px, py});
        std::sort(points_.begin(), points_.end());
        for (size_t i = 0; i < points_.size(); ++i)
          if (points_[i] == std::pair{px, py})
            selected_ = i;
      }
      redraw();
      return 1;
    }
    if (event == FL_DRAG && selected_ >= 0) {
      int lo = selected_ ? points_[selected_ - 1].first + 1 : 0,
          hi = selected_ + 1 < int(points_.size()) ? points_[selected_ + 1].first - 1 : 255;
      if (selected_ == 0)
        lo = hi = 0;
      if (selected_ == int(points_.size()) - 1)
        lo = hi = 255;
      points_[selected_] = {std::clamp(px, lo, hi), py};
      redraw();
      return 1;
    }
    if (event == FL_KEYDOWN && (Fl::event_key() == FL_Delete || Fl::event_key() == FL_BackSpace) &&
        selected_ > 0 && selected_ + 1 < int(points_.size())) {
      points_.erase(points_.begin() + selected_);
      selected_ = -1;
      redraw();
      return 1;
    }
    return Fl_Widget::handle(event);
  }
};
// Shared window furniture only. Every caller supplies an explicit, domain-specific
// layout and converts its controls to engine arguments; no command schema parser.
class Dialog {
  artcraft::Application app_;
  DialogBody *body_;
  bool accepted_ = false;
  int next_ = 24;

public:
  explicit Dialog(const std::string &title, int height = 460)
      : app_({"org.domed.PhotoCraft.Dialog", title, 780, height, false}) {
    app_.window().set_modal();
    app_.window().begin();
    body_ = new DialogBody(0, app_.content_top(), 780, height - app_.content_top());
    body_->begin();
    app_.add_command({"accept",
                      "&Dialog/&Apply",
                      FL_Enter,
                      false,
                      [this] {
                        accepted_ = true;
                        app_.quit();
                      },
                      {}});
    app_.add_command({"cancel", "&Dialog/&Cancel", FL_Escape, false, [this] { app_.quit(); }, {}});
  }
  Fl_Input *text(const char *label, const std::string &value) {
    auto *input = new Fl_Input(310, app_.content_top() + next_, 430, 34, label);
    input->value(value.c_str());
    next_ += 48;
    return input;
  }
  Fl_Value_Input *number(const char *label, double value, double low, double high,
                         double step = 1) {
    auto *input = new Fl_Value_Input(310, app_.content_top() + next_, 200, 34, label);
    input->range(low, high);
    input->step(step);
    input->value(value);
    next_ += 48;
    return input;
  }
  Fl_Choice *choice(const char *label, std::initializer_list<const char *> values,
                    int selected = 0) {
    auto *input = new NativeChoice(310, app_.content_top() + next_, 430, 34, label);
    for (auto value : values)
      input->add(value);
    input->value(selected);
    next_ += 48;
    return input;
  }
  Fl_Check_Button *check(const char *label, bool value) {
    auto *input = new Fl_Check_Button(310, app_.content_top() + next_, 430, 34, label);
    input->value(value);
    next_ += 48;
    return input;
  }
  ColorPicker *colour(double r, double g, double b) {
    auto *input = new ColorPicker(30, app_.content_top() + next_, 700, 260);
    input->rgb(r, g, b);
    next_ += 280;
    return input;
  }
  int area(int height) {
    int at = app_.content_top() + next_;
    next_ += height;
    return at;
  }
  void notice(const char *message) {
    auto *label = new Fl_Box(30, app_.content_top() + next_, 710, 100);
    label->copy_label(message);
    label->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_WRAP);
    next_ += 112;
  }
  bool run(const char *accept_label = "Apply") {
    int height = std::max(app_.window().h(), app_.content_top() + next_ + 64);
    app_.window().size(780, height);
    auto *ok = new Fl_Button(590, height - 48, 150, 34, accept_label);
    ok->callback([](Fl_Widget *, void *p) { static_cast<Dialog *>(p)->app_.invoke("accept"); },
                 this);
    auto *cancel = new Fl_Button(420, height - 48, 150, 34, "Cancel");
    cancel->callback([](Fl_Widget *, void *p) { static_cast<Dialog *>(p)->app_.quit(); }, this);
    body_->end();
    app_.window().end();
    body_->resize(0, app_.content_top(), 780, height - app_.content_top());
    app_.style();
    app_.show();
    while (app_.window().shown())
      Fl::wait();
    return accepted_;
  }
};
} // namespace dome_photo
