// SPDX-License-Identifier: MIT
#pragma once
#include "gpu-image.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
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
  Fl_Color_Chooser *colour(double r, double g, double b) {
    auto *input = new Fl_Color_Chooser(30, app_.content_top() + next_, 700, 260);
    input->rgb(r, g, b);
    for (int i = 0; i < input->children(); ++i)
      if (auto *old = dynamic_cast<Fl_Choice *>(input->child(i))) {
        old->hide();
        auto *parent = Fl_Group::current();
        input->begin();
        auto *mode = new NativeChoice(old->x(), old->y(), old->w(), old->h());
        mode->copy(old->menu());
        mode->value(old->value());
        mode->callback(
            [](Fl_Widget *widget, void *data) {
              static_cast<Fl_Color_Chooser *>(data)->mode(
                  static_cast<Fl_Choice *>(widget)->value());
            },
            input);
        input->end();
        Fl_Group::current(parent);
        break;
      }
    next_ += 280;
    return input;
  }
  int area(int height) {
    int at = app_.content_top() + next_;
    next_ += height;
    return at;
  }
  bool run() {
    int height = std::max(app_.window().h(), app_.content_top() + next_ + 64);
    app_.window().size(780, height);
    auto *ok = new Fl_Button(590, height - 48, 150, 34, "Apply");
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
