// SPDX-License-Identifier: MIT
#include "gpu-image.hpp"
#include "photo-widgets.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Value_Input.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <array>
#include <artcraft/application.hpp>
#include <artcraft/upstream_session.hpp>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
using Json = nlohmann::json;
namespace {
using dome_office::GpuImage;
using dome_office::ScreenRect;
class Photo;
// Original native geometry for familiar tool affordances; no third-party icon
// assets.
class ToolButton : public Fl_Button {
  std::string tool_;
  void draw() override {
    draw_box(box(), value() ? selection_color() : color());
    fl_push_clip(x(), y(), w(), h());
    fl_color(active_r() ? labelcolor() : FL_DARK3);
    fl_line_style(FL_SOLID, 1);
    int cx = x() + w() / 2, cy = y() + h() / 2;
    if (tool_ == "move") {
      fl_line(cx - 8, cy, cx + 8, cy);
      fl_line(cx, cy - 8, cx, cy + 8);
      fl_line(cx - 8, cy, cx - 5, cy - 3);
      fl_line(cx + 8, cy, cx + 5, cy + 3);
      fl_line(cx, cy - 8, cx - 3, cy - 5);
      fl_line(cx, cy + 8, cx + 3, cy + 5);
    } else if (tool_ == "marquee") {
      fl_line_style(FL_DOT, 1);
      fl_rect(cx - 8, cy - 6, 16, 12);
    } else if (tool_ == "lasso") {
      fl_arc(cx - 8, cy - 7, 16, 12, 0, 320);
      fl_line(cx - 5, cy + 3, cx - 1, cy + 8);
    } else if (tool_ == "selection") {
      fl_line(cx - 7, cy + 7, cx + 5, cy - 5);
      fl_line(cx + 4, cy - 9, cx + 4, cy - 1);
      fl_line(cx, cy - 5, cx + 8, cy - 5);
    } else if (tool_ == "crop") {
      fl_line(cx - 8, cy - 4, cx + 5, cy - 4, cx + 5, cy + 9);
      fl_line(cx - 4, cy - 9, cx - 4, cy + 5, cx + 9, cy + 5);
    } else if (tool_ == "frame") {
      fl_rect(cx - 8, cy - 6, 16, 12);
      fl_line(cx - 8, cy - 6, cx + 8, cy + 6);
      fl_line(cx + 8, cy - 6, cx - 8, cy + 6);
    } else if (tool_ == "eyedropper") {
      fl_line(cx - 7, cy + 7, cx + 6, cy - 6);
      fl_line(cx - 4, cy + 7, cx + 8, cy - 5);
      fl_line(cx + 2, cy - 7, cx + 8, cy - 1);
    } else if (tool_ == "healing") {
      fl_rect(cx - 8, cy - 5, 16, 10);
      fl_line(cx - 2, cy - 4, cx - 2, cy + 4);
      fl_line(cx + 2, cy - 4, cx + 2, cy + 4);
    } else if (tool_ == "brush" || tool_ == "history") {
      fl_line(cx - 3, cy + 3, cx + 7, cy - 7);
      fl_line(cx - 1, cy + 5, cx + 9, cy - 5);
      fl_polygon(cx - 7, cy + 2, cx - 1, cy + 5, cx - 8, cy + 8);
      if (tool_ == "history")
        fl_arc(cx - 10, cy - 10, 10, 10, 0, 270);
    } else if (tool_ == "clone") {
      fl_rect(cx - 8, cy + 2, 16, 5);
      fl_rect(cx - 3, cy - 8, 6, 9);
    } else if (tool_ == "eraser") {
      fl_loop(cx - 8, cy + 1, cx, cy - 7, cx + 8, cy + 1, cx, cy + 8);
      fl_line(cx - 4, cy - 3, cx + 4, cy + 5);
    } else if (tool_ == "gradient") {
      for (int i = 0; i < 14; ++i) {
        fl_color(fl_rgb_color(i * 18, i * 18, i * 18));
        fl_rectf(cx - 7 + i, cy - 6, 1, 12);
      }
      fl_color(labelcolor());
      fl_rect(cx - 8, cy - 7, 16, 14);
    } else if (tool_ == "blur") {
      fl_loop(cx, cy - 9, cx - 7, cy + 4, cx, cy + 8, cx + 7, cy + 4);
    } else if (tool_ == "dodge") {
      fl_arc(cx - 7, cy - 8, 12, 10, 0, 360);
      fl_line(cx + 2, cy + 1, cx + 7, cy + 8);
    } else if (tool_ == "pen") {
      fl_loop(cx, cy - 9, cx - 6, cy + 2, cx, cy + 8, cx + 6, cy + 2);
      fl_line(cx, cy - 9, cx, cy + 2);
      fl_rect(cx - 1, cy + 1, 3, 3);
    } else if (tool_ == "type") {
      fl_font(labelfont(), labelsize());
      fl_draw("T", cx - 4, cy + 6);
    } else if (tool_ == "path") {
      fl_loop(cx - 5, cy - 8, cx - 5, cy + 8, cx, cy + 3, cx + 5, cy + 5);
    } else if (tool_ == "shape") {
      fl_rect(cx - 8, cy - 6, 16, 12);
    } else if (tool_ == "hand") {
      fl_rect(cx - 6, cy - 2, 12, 9);
      for (int i = 0; i < 4; ++i)
        fl_line(cx - 5 + i * 3, cy - 2, cx - 5 + i * 3, cy - 8 + (i % 2));
      fl_line(cx - 6, cy + 1, cx - 9, cy - 2);
    } else {
      fl_arc(cx - 8, cy - 8, 12, 12, 0, 360);
      fl_line(cx + 2, cy + 2, cx + 8, cy + 8);
    }
    fl_line_style(0);
    fl_pop_clip();
  }

public:
  std::function<void()> variants;
  bool holding_ = false, flyout_ = false;
  static void held(void *p) {
    auto &self = *static_cast<ToolButton *>(p);
    if (self.holding_ && self.variants) {
      self.flyout_ = true;
      self.variants();
    }
  }
  ~ToolButton() { Fl::remove_timeout(held, this); }
  int handle(int event) override {
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
      holding_ = true;
      return 1;
    }
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
      holding_ = true;
      flyout_ = false;
      Fl::add_timeout(.35, held, this);
    }
    if (event == FL_RELEASE) {
      Fl::remove_timeout(held, this);
      holding_ = false;
      if (Fl::event_button() == FL_RIGHT_MOUSE) {
        if (variants)
          variants();
        return 1;
      }
      if (flyout_) {
        value(1);
        return 1;
      }
    }
    return Fl_Button::handle(event);
  }
  ToolButton(int x, int y, int w, int h, std::string tool)
      : Fl_Button(x, y, w, h), tool_(std::move(tool)) {}
};
class ColorPairButton : public Fl_Button {
  std::function<std::pair<std::string, std::string>()> colors_;
  void draw() override {
    draw_box(box(), color());
    auto colors = colors_();
    auto paint = [&](const std::string &hex, int X, int Y, int W, int H) {
      unsigned n = std::stoul(hex.substr(1), nullptr, 16);
      fl_color(fl_rgb_color(n >> 16, (n >> 8) & 255, n & 255));
      fl_rectf(X, Y, W, H);
      fl_color(FL_FOREGROUND_COLOR);
      fl_rect(X, Y, W, H);
    };
    paint(colors.second, x() + 20, y() + 8, 24, 17);
    paint(colors.first, x() + 7, y() + 1, 25, 20);
  }

public:
  ColorPairButton(int x, int y, int w, int h,
                  std::function<std::pair<std::string, std::string>()> colors)
      : Fl_Button(x, y, w, h, "FG"), colors_(std::move(colors)) {
    tooltip("Foreground and background colours; click to edit foreground");
  }
};
class PhotoLayers : public Fl_Hold_Browser {
  std::map<unsigned long, GpuImage> thumbnails_;
  std::map<unsigned long, bool> visible_;
  int handle(int event) override {
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
      auto *item = find_item(Fl::event_y());
      if (item)
        value(lineno(item));
      return 1;
    }
    if (event == FL_RELEASE && Fl::event_button() == FL_RIGHT_MOUSE) {
      if (context)
        context(value());
      return 1;
    }
    if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE && Fl::event_x() < x() + 26) {
      auto *item = find_item(Fl::event_y());
      if (item && visibility) {
        auto id = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(data(lineno(item))));
        visibility(id);
        return 1;
      }
    }
    return Fl_Hold_Browser::handle(event);
  }
  int item_height(void *) const override { return 52; }
  int item_width(void *item) const override {
    const char *text = item_text(item);
    return 92 + int(text ? std::string(text).size() * 8 : 0);
  }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    int row = lineno(item);
    auto id = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(data(row)));
    fl_push_clip(X, Y, W, H);
    int clip_x = std::max(X, x() + 2), clip_y = std::max(Y, y() + 2),
        clip_right = std::min(X + W, x() + w() - Fl::scrollbar_size() - 2),
        clip_bottom = std::min(Y + H, y() + h() - 2);
    dome_office::GpuClip content_clip(
        *const_cast<PhotoLayers *>(this),
        {clip_x, clip_y, std::max(0, clip_right - clip_x), std::max(0, clip_bottom - clip_y)});
    fl_color(fl_contrast(FL_FOREGROUND_COLOR, selected(row) ? selection_color() : color()));
    if (auto shown = visible_.find(id); shown == visible_.end() || shown->second) {
      fl_arc(X + 4, Y + H / 2 - 4, 14, 8, 0, 360);
      fl_rectf(X + 10, Y + H / 2 - 1, 3, 3);
    }
    auto image = thumbnails_.find(id);
    if (image != thumbnails_.end() && !image->second.empty()) {
      double scale = std::min(42. / image->second.width(), 42. / image->second.height());
      int iw = int(image->second.width() * scale), ih = int(image->second.height() * scale);
      int left = std::max(X, x() + 2), top = std::max(Y, y() + 2),
          right = std::min(X + W, x() + w() - Fl::scrollbar_size() - 2),
          bottom = std::min(Y + H, y() + h() - 2);
      image->second.draw(*const_cast<PhotoLayers *>(this),
                         {X + 28 + (42 - iw) / 2, Y + (H - ih) / 2, iw, ih},
                         {left, top, std::max(0, right - left), std::max(0, bottom - top)});
    } else {
      fl_color(FL_DARK3);
      fl_rect(X + 28, Y + 5, 42, 42);
    }
    fl_color(fl_contrast(FL_FOREGROUND_COLOR, selected(row) ? selection_color() : color()));
    fl_font(textfont(), textsize());
    auto *text = item_text(item);
    fl_draw(text ? text : "", X + 78, Y + 4, std::max(1, W - 82), H - 8,
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fl_pop_clip();
  }

public:
  using Fl_Hold_Browser::Fl_Hold_Browser;
  std::function<void(unsigned long)> visibility;
  std::function<void(int)> context;
  void visibility_state(unsigned long id, bool value) { visible_[id] = value; }
  void thumbnail(const Json &data) {
    auto id = data.at("layer").get<unsigned long>();
    thumbnails_[id].set(data.at("png"));
    while (thumbnails_.size() > 64) {
      auto old = thumbnails_.begin();
      if (old->first == id)
        ++old;
      thumbnails_.erase(old);
    }
  }
  void reset_images() { thumbnails_.clear(); }
};
class ContextBrowser : public Fl_Hold_Browser {
public:
  std::function<void(int)> context;
  using Fl_Hold_Browser::Fl_Hold_Browser;
  int handle(int event) override {
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
      auto *item = find_item(Fl::event_y());
      if (item)
        value(lineno(item));
      return 1;
    }
    if (event == FL_RELEASE && Fl::event_button() == FL_RIGHT_MOUSE) {
      if (context)
        context(value());
      return 1;
    }
    return Fl_Hold_Browser::handle(event);
  }
};
class InlineTypeInput : public Fl_Multiline_Input {
public:
  std::function<void(bool)> finish;
  using Fl_Multiline_Input::Fl_Multiline_Input;
  int handle(int event) override {
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
      if (finish)
        finish(false);
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Enter && Fl::event_state(FL_CTRL | FL_META)) {
      if (finish)
        finish(true);
      return 1;
    }
    return Fl_Multiline_Input::handle(event);
  }
};
class PhotoFrame : public Fl_Group {
  Photo &owner_;
  int grip_ = 0, anchor_ = 0;

public:
  PhotoFrame(int x, int y, int w, int h, Photo &p) : Fl_Group(x, y, w, h), owner_(p) {}
  void resize(int x, int y, int w, int h) override;
  int handle(int event) override;
};
class PhotoCanvas : public Fl_Widget {
  Photo &owner_;
  GpuImage image_;
  ScreenRect paper_{};
  double scale_ = 1, pan_x_ = 0, pan_y_ = 0;
  Json points_ = Json::array();
  int start_x_ = 0, start_y_ = 0;
  bool panning_ = false, moving_selection_ = false;
  std::string gesture_mode_, gesture_tool_;
  int gesture_variant_ = 0;
  bool selected(double px, double py) const;
  void finish_polygon();
  void finish_path(bool closed);
  Json crop_ = Json();
  void apply_crop();
  void cancel_crop() {
    crop_ = Json();
    points_ = Json::array();
    redraw();
  }
  int selection_phase_ = 0;
  std::chrono::steady_clock::time_point next_selection_frame_{};
  Json clone_source_, clone_offset_;
  std::array<unsigned, 256> bins_{};
  void draw() override;
  int handle(int event) override;

public:
  PhotoCanvas(int x, int y, int w, int h, Photo &p) : Fl_Widget(x, y, w, h), owner_(p) {
    copy_label("Image canvas");
  }
  void refresh(const Json &state);
  void geometry();
  int ruler_size() const;
  const GpuImage &image() const { return image_; }
  double scale() const { return scale_; }
  const auto &histogram() const { return bins_; }
  const auto &paper() const { return paper_; }
  void reset_pan() { pan_x_ = pan_y_ = 0; }
  friend class Photo;
  friend struct PhotoHarness;
};
class PhotoNavigator : public Fl_Widget {
  Photo &owner_;
  bool histogram_;
  void draw() override;
  int handle(int event) override;

public:
  PhotoNavigator(int x, int y, int w, int h, Photo &p, bool histogram = false)
      : Fl_Widget(x, y, w, h), owner_(p), histogram_(histogram) {};
};
struct Options {
  std::filesystem::path engine, registry = DOME_PHOTO_REGISTRY, workspace, document, report,
                                capture, save;
  int lifetime = 0, width = 1280, height = 840;
  bool tray = false;
};
class Photo {
  artcraft::Application app_;
  Options options_;
  Json config_, snapshot_ = Json::object();
  std::unique_ptr<artcraft::UpstreamSession> engine_;
  std::future<Json> job_;
  std::function<void()> after_idle_;
  std::deque<std::pair<std::string, Json>> queued_;
  bool paint_prompt_ = false;
  bool busy_ = true, dirty_ = false, panels_ = true, rulers_ = true;
  std::string error_, operation_, tool_ = "move", foreground_ = "#000000", background_ = "#ffffff";
  double zoom_ = 0;
  int options_font_size_ = 0;
  int dock_width_ = 288, color_height_ = 190, properties_height_ = 198;
  std::filesystem::path path_;
  dome_photo::ContextMenu *context_;
  Fl_Multiline_Input *type_editor_;
  Fl_Value_Input *type_size_;
  Fl_Choice *type_align_;
  Fl_Button *type_commit_, *type_cancel_, *crop_commit_, *crop_cancel_;
  double type_x_ = 0, type_y_ = 0;
  PhotoFrame *frame_;
  Fl_Scroll *options_bar_;
  Fl_Group *toolbox_, *dock_, *rail_, *color_panel_, *properties_panel_, *layers_panel_, *foot_;
  Fl_Tabs *documents_, *top_tabs_, *middle_tabs_, *bottom_tabs_;
  Fl_Group *navigator_panel_, *history_panel_, *histogram_panel_, *channels_panel_, *paths_panel_;
  PhotoCanvas *canvas_;
  PhotoNavigator *navigator_, *histogram_;
  Fl_Box *tool_label_, *dimensions_, *status_;
  Fl_Input *color_, *layer_filter_;
  Fl_Value_Input *brush_size_, *brush_opacity_, *hardness_, *layer_opacity_;
  Fl_Choice *tool_variant_, *selection_operation_;
  Fl_Value_Input *feather_, *tolerance_;
  Fl_Check_Button *antialias_, *contiguous_, *sample_all_, *aligned_;
  std::map<std::string, int> tool_variants_;
  Fl_Choice *paint_mode_, *blend_, *workspace_, *zoom_choice_;
  PhotoLayers *layers_;
  Fl_Hold_Browser *history_, *channels_, *paths_;
  std::vector<unsigned long> layer_ids_;
  std::vector<std::string> channel_names_, path_names_;
  std::map<std::string, Json> catalogue_;
  std::vector<artcraft::Command> menu_commands_;
  std::map<std::string, bool> enabled_;
  std::map<std::string, Fl_Button *> tools_;
  std::string document_key_;
  int thumbnail_row_ = 0, thumbnail_document_ = -1;
  std::string thumbnail_viewport_;
  std::map<unsigned long, std::pair<std::string, Json>> thumbnail_cache_;
  struct Binding {
    std::function<void()> action;
  };
  std::vector<std::unique_ptr<Binding>> bindings_;
  std::set<std::string> registered_;
  friend struct PhotoHarness;
  friend class PhotoCanvas;
  friend class PhotoFrame;
  friend class PhotoNavigator;
  static void callback(Fl_Widget *, void *data) {
    auto action = static_cast<Binding *>(data)->action;
    try {
      action();
    } catch (const std::exception &e) {
      fl_alert("%s", e.what());
    }
  }
  void bind(Fl_Widget *widget, std::function<void()> action) {
    auto p = std::make_unique<Binding>(Binding{std::move(action)});
    widget->callback(callback, p.get());
    bindings_.push_back(std::move(p));
  }
  Fl_Button *button(int x, int y, int w, int h, const char *label, std::function<void()> action) {
    auto *b = new Fl_Button(x, y, w, h, label);
    bind(b, std::move(action));
    return b;
  }
  void menu(std::string id, std::string path, std::function<void()> action, int shortcut = 0,
            bool engine = false) {
    if (!registered_.insert(id).second)
      return;
    menu_commands_.push_back(
        {id, path, shortcut, false, std::move(action), [this, id, engine] {
           return artcraft::CommandState{
               !busy_ && (!engine || (enabled_[id] && native_control(id))) &&
                   (id != "window.hide" || app_.platform_info().tray_available),
               false};
         }});
  }
  void status() {
    if (status_)
      status_->copy_label((busy_ ? "Working..." : error_.empty() ? "Ready" : error_).c_str());
    if (canvas_)
      canvas_->redraw();
  }
  void start(std::string operation, std::function<Json()> action) {
    if (busy_ && operation != "startup")
      return;
    busy_ = true;
    operation_ = std::move(operation);
    error_.clear();
    status();
    std::set<unsigned long> wanted;
    int first = std::max(0, layers_->topline() - 1);
    for (int i = first; i < std::min(int(layer_ids_.size()), first + 12); ++i)
      wanted.insert(layer_ids_[i]);
    job_ = std::async(std::launch::async, [this, wanted = std::move(wanted),
                                           action = std::move(action)] {
      auto result = action();
      auto state = engine_->snapshot();
      // Keep authoritative path/dirty state even if a preview cannot render.
      try {
        state["view"]["raster"] = engine_->render();
      } catch (const std::exception &error) {
        state["view"]["render_error"] = error.what();
      }
      state["view"]["paths"] = engine_->execute("path.list", Json::object());
      const auto &document = state.at("document");
      int docindex = state["view"].value("session", Json::object()).value("active", 0);
      if (docindex != thumbnail_document_ || operation_ == "edit.undo" ||
          operation_ == "edit.redo" || operation_.starts_with("image.")) {
        thumbnail_cache_.clear();
        thumbnail_document_ = docindex;
      }
      auto layerlist = document.value("layers", Json::array());
      const auto active = document.value("activeLayer", 0ul);
      state["view"]["layer_thumbnails"] = Json::array();
      int begin = std::clamp(thumbnail_row_, 0, std::max(0, int(layerlist.size()) - 1));
      for (int i = 0; i < int(layerlist.size()) && !state["view"].contains("render_error"); ++i) {
        const auto &layer = layerlist[i];
        auto id = layer.value("id", 0ul);
        if (id != active &&
            (!wanted.empty() ? !wanted.contains(id) : (i < begin || i >= begin + 12)))
          continue;
        auto key = layer.dump();
        if (id == active)
          key += std::to_string(document.value("revision", 0ul));
        auto found = thumbnail_cache_.find(id);
        if (found == thumbnail_cache_.end() || found->second.first != key) {
          try {
            auto image = engine_->image_preview({{"layer", id}, {"max_side", 64}});
            image["layer"] = id;
            thumbnail_cache_[id] = {key, std::move(image)};
          } catch (const std::exception &error) {
            state["view"]["thumbnail_error"] = error.what();
            continue;
          }
        }
        state["view"]["layer_thumbnails"].push_back(thumbnail_cache_.at(id).second);
      }
      while (thumbnail_cache_.size() > 64)
        thumbnail_cache_.erase(thumbnail_cache_.begin());
      return Json{{"result", std::move(result)}, {"snapshot", std::move(state)}};
    });
  }
  void execute(std::string id, Json params = Json::object()) {
    if (busy_) {
      if (queued_.size() >= 64) {
        error_ = "Wait for pending edits";
        status();
        return;
      }
      // Each gesture is an undoable edit; merging identical command IDs loses
      // strokes.
      queued_.push_back({std::move(id), std::move(params)});
      return;
    }
    // Validate against the latest snapshot when a queued stroke is drained.
    // In particular type.create may still be running when the brush gesture begins.
    if (id.starts_with("paint.") && params.contains("points") &&
        params.value("target", std::string("pixels")) == "pixels") {
      const auto doc = snapshot_.value("document", Json::object());
      const auto target = doc.value("channels", Json::object()).value("target", Json::object());
      if (!doc.value("quickMask", false) &&
          target.value("kind", std::string("composite")) != "alpha") {
        auto active = doc.value("activeLayer", 0ul);
        for (const auto &layer : doc.value("layers", Json::array())) {
          if (layer.value("id", 0ul) != active)
            continue;
          auto kind = layer.value("kind", std::string("Pixel"));
          if (kind == "Type" || kind == "Shape" || kind == "Fill" || kind == "Smart Object") {
            paint_prompt_ = true;
            dome_photo::Dialog dialog("Rasterize Layer", 280);
            dialog.notice("Painting requires pixels. Rasterizing converts this layer's editable "
                          "text, vectors or smart content into pixels. You can undo this change.");
            bool accepted = dialog.run("Rasterize");
            paint_prompt_ = false;
            if (accepted) {
              queued_.push_front({std::move(id), std::move(params)});
              start("layer.rasterize.layer", [this, active] {
                return engine_->execute("layer.rasterize.layer", {{"layer", active}});
              });
            } else {
              // Discard brush gestures collected before the user cancelled.
              std::erase_if(
                  queued_, [](const auto &command) { return command.first.starts_with("paint."); });
            }
            return;
          }
        }
      }
    }
    start(id, [this, id, params] {
      if (id == "native.sampleColor") {
        auto rgba = engine_->execute("document.sampleColor", params);
        Json rgb = Json::array({rgba.at(0), rgba.at(1), rgba.at(2)});
        engine_->execute("tools.setColors", {{"foreground", rgb}});
        return rgba;
      }
      return engine_->execute(id, params);
    });
  }
#include "photo-context.inc"
#include "photo-controls.inc"
  bool discard(bool all = false) {
    bool dirty = dirty_;
    if (all)
      for (const auto &document : snapshot_.value("view", Json::object())
                                      .value("session", Json::object())
                                      .value("documents", Json::array()))
        dirty = dirty || document.value("dirty", false);
    return !dirty || fl_choice("Discard unsaved image changes?", "Cancel", "Discard", nullptr) == 1;
  }
  std::filesystem::path choose(bool save, const char *extension = ".pcraft") {
    Fl_Native_File_Chooser chooser;
    chooser.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                      : Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.title(save ? "Save image" : "Open image");
    chooser.directory(options_.workspace.c_str());
    if (save) {
      auto name = std::string("Untitled") + extension;
      chooser.preset_file(name.c_str());
      chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    }
    if (chooser.show() || !chooser.filename())
      return {};
    return std::filesystem::absolute(chooser.filename());
  }
  void save(bool as = false) {
    auto path =
        as || path_.empty() ? (options_.save.empty() ? choose(true) : options_.save) : path_;
    if (path.empty())
      return;
    start("save", [this, path] { return engine_->save(path); });
  }
  void new_document() {
    if (busy_)
      return;
    dome_photo::Dialog dialog("New Document", 520);
    auto *name = dialog.text("Name", "Untitled");
    auto *width = dialog.number("Width (pixels)", 1200, 1, 30000);
    auto *height = dialog.number("Height (pixels)", 800, 1, 30000);
    auto *background = dialog.choice("Background contents", {"White", "Black", "Transparent"});
    auto *mode = dialog.choice("Color mode", {"RGB Color", "Grayscale", "CMYK Color", "Lab Color"});
    auto *depth = dialog.choice("Bit depth", {"8 bit", "16 bit", "32 bit"});
    if (dialog.run()) {
      Json params = {
          {"name", name->value()},
          {"width", int(width->value())},
          {"height", int(height->value())},
          {"background",
           std::array<const char *, 3>{"white", "black", "transparent"}[background->value()]},
          {"mode", std::array<const char *, 4>{"rgb", "gray", "cmyk", "lab"}[mode->value()]},
          {"depth", std::array<int, 3>{8, 16, 32}[depth->value()]}};
      start("new", [this, params] { return engine_->tool("doc_new", params); });
    }
  }
  std::vector<std::string> variants(const std::string &tool) const {
    if (tool == "marquee")
      return {"Rectangular marquee", "Elliptical marquee", "Single row", "Single column"};
    if (tool == "lasso")
      return {"Lasso", "Polygonal lasso"};
    if (tool == "selection")
      return {"Magic wand", "Quick selection"};
    if (tool == "shape")
      return {"Rectangle", "Ellipse"};
    if (tool == "gradient")
      return {"Linear", "Radial", "Angle", "Reflected", "Diamond"};
    return {};
  }
  int variant() const { return tool_variants_.contains(tool_) ? tool_variants_.at(tool_) : 0; }
  void update_options() {
    bool selecting = tool_ == "marquee" || tool_ == "lasso" || tool_ == "selection";
    bool wand = tool_ == "selection" && variant() == 0;
    bool quick = tool_ == "selection" && variant() == 1;
    bool paint = tool_ == "brush" || tool_ == "eraser" || tool_ == "clone" || tool_ == "healing" ||
                 tool_ == "history" || tool_ == "blur" || tool_ == "dodge";
    auto show = [](Fl_Widget *widget, bool visible) {
      if (visible)
        widget->show();
      else
        widget->hide();
    };
    show(brush_size_, paint || quick);
    show(paint_mode_, (paint && tool_ != "healing" && tool_ != "blur" && tool_ != "dodge") ||
                          tool_ == "gradient");
    show(brush_opacity_, paint || tool_ == "gradient");
    show(hardness_, paint);
    brush_opacity_->copy_label(tool_ == "dodge"  ? "Exposure"
                               : tool_ == "blur" ? "Strength"
                                                 : "Opacity");
    show(aligned_, tool_ == "clone");
    show(crop_commit_, tool_ == "crop");
    show(crop_cancel_, tool_ == "crop");
    for (Fl_Widget *widget :
         std::vector<Fl_Widget *>{type_size_, type_align_, type_commit_, type_cancel_})
      show(widget, tool_ == "type");
    auto choices = variants(tool_);
    tool_variant_->clear();
    for (auto &choice : choices)
      tool_variant_->add(choice.c_str());
    tool_variant_->value(variant());
    show(tool_variant_, !choices.empty());
    show(selection_operation_, selecting);
    selection_operation_->mode(3, quick ? FL_MENU_INACTIVE : 0);
    if (quick && selection_operation_->value() == 3)
      selection_operation_->value(0);
    options_bar_->scroll_to(0, 0);
    show(feather_, selecting && tool_ != "selection");
    show(tolerance_, wand);
    show(antialias_, selecting && !quick);
    show(contiguous_, wand);
    show(sample_all_, tool_ == "selection");
    layout();
    options_bar_->redraw();
  }
  void set_tool(const std::string &tool) {
    canvas_->points_ = Json::array();
    canvas_->panning_ = false;
    canvas_->moving_selection_ = false;
    finish_type(tool_ == "type" && type_editor_->visible());
    if (tool_ != tool)
      canvas_->cancel_crop();
    tool_ = tool;
    for (auto &[name, b] : tools_) {
      b->value(name == tool);
      b->redraw();
    }
    std::string label = tool;
    std::replace(label.begin(), label.end(), '_', ' ');
    if (!label.empty())
      label[0] = std::toupper(static_cast<unsigned char>(label[0]));
    tool_label_->copy_label(label.c_str());
    tool_label_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    update_options();
    canvas_->take_focus();
    canvas_->redraw();
  }
  std::string selection_mode() const {
    if (Fl::event_state(FL_ALT) && Fl::event_state(FL_SHIFT))
      return "intersect";
    if (Fl::event_state(FL_ALT))
      return "subtract";
    if (Fl::event_state(FL_SHIFT))
      return "add";
    static const char *modes[] = {"replace", "add", "subtract", "intersect"};
    return modes[std::clamp(selection_operation_->value(), 0, 3)];
  }
  std::string color() const {
    std::string value = color_->value();
    if (value.size() != 7 || value[0] != '#' ||
        value.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
      throw std::runtime_error("Use a #RRGGBB foreground colour");
    return value;
  }
  void foreground_dialog(bool background = false) {
    unsigned value = std::stoul((background ? background_ : foreground_).substr(1), nullptr, 16);
    dome_photo::Dialog dialog(background ? "Background Color" : "Foreground Color", 420);
    auto *picker = dialog.colour(double(value >> 16) / 255, double((value >> 8) & 255) / 255,
                                 double(value & 255) / 255);
    if (dialog.run()) {
      char hex[8];
      std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", int(std::round(picker->r() * 255)),
                    int(std::round(picker->g() * 255)), int(std::round(picker->b() * 255)));
      if (background)
        background_ = hex;
      else
        foreground_ = hex;
      color_->value(foreground_.c_str());
      execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
    }
  }
  void zoom(double value) {
    zoom_ = std::clamp(value, .05, 32.);
    canvas_->geometry();
    canvas_->redraw();
  }
  void refresh() {
    auto doc = snapshot_.value("document", Json::object());
    auto session = snapshot_.value("view", Json::object()).value("session", Json::object());
    enabled_.clear();
    for (auto c : snapshot_.value("commands", Json::array()))
      enabled_[c.at("id")] = c.value("enabled", false);
    auto fg = session.value("foreground", Json());
    if (fg.is_array() && fg.size() >= 3) {
      char hex[8];
      std::snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                    std::clamp(int(fg[0].get<double>() * 255), 0, 255),
                    std::clamp(int(fg[1].get<double>() * 255), 0, 255),
                    std::clamp(int(fg[2].get<double>() * 255), 0, 255));
      foreground_ = hex;
      if (Fl::focus() != color_)
        color_->value(foreground_.c_str());
    }
    int active = session.value("active", 0);
    dirty_ = false;
    for (auto d : session.value("documents", Json::array()))
      if (d.value("index", -1) == active) {
        dirty_ = d.value("dirty", false);
        path_ = d.value("path", Json()).is_string()
                    ? std::filesystem::path(d.at("path").get<std::string>())
                    : std::filesystem::path();
      }
    auto tabs = session.value("documents", Json::array());
    std::string key;
    for (auto d : tabs)
      key += std::to_string(d.value("index", 0)) + d.value("name", std::string()) +
             (d.value("dirty", false) ? " *" : "") + "\n";
    if (key != document_key_) {
      document_key_ = key;
      auto *parent = Fl_Group::current();
      documents_->clear();
      documents_->begin();
      for (auto d : tabs) {
        auto *tab = new Fl_Group(documents_->x(), documents_->y() + 30, documents_->w(), 1);
        auto label =
            d.value("name", std::string("Untitled")) + (d.value("dirty", false) ? " *" : "");
        tab->copy_label(label.c_str());
        tab->user_data(reinterpret_cast<void *>(intptr_t(d.value("index", 0)) + 1));
        tab->end();
      }
      documents_->end();
      Fl_Group::current(parent);
      app_.style();
    }
    for (int i = 0; i < documents_->children(); ++i)
      if (intptr_t(documents_->child(i)->user_data()) - 1 == active)
        documents_->value(documents_->child(i));
    auto title =
        doc.value("name", std::string("Untitled")) + (dirty_ ? " *" : "") + " - PhotoCraft";
    app_.window().copy_label(title.c_str());
    layers_->clear();
    layer_ids_.clear();
    int selected = 0;
    auto selected_id = doc.value("activeLayer", 0ul);
    std::string filter = layer_filter_->value();
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (auto layer : doc.value("layers", Json::array())) {
      auto name = layer.value("name", std::string());
      auto lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (!filter.empty() && lower.find(filter) == std::string::npos)
        continue;
      auto id = layer.value("id", 0ul);
      layer_ids_.push_back(id);
      std::string kind = layer.value("kind", std::string("Pixel"));
      auto label = std::string(layer.value("visible", true) ? "[o] " : "[ ] ") + "[" +
                   kind.substr(0, 3) + "] " + name;
      layers_->add(name.c_str(), reinterpret_cast<void *>(uintptr_t(id)));
      layers_->visibility_state(id, layer.value("visible", true));
      if (id == selected_id) {
        selected = layer_ids_.size();
        layer_opacity_->value(layer.value("opacity", 1.) * 100);
        auto b = layer.value("blend", std::string("Normal"));
        blend_->value(blend_->find_index(b.c_str()));
      }
    }
    layers_->value(selected);
    if (operation_ == "document" || operation_ == "open" || operation_ == "new" ||
        operation_ == "startup")
      layers_->reset_images();
    for (const auto &thumbnail :
         snapshot_.value("view", Json::object()).value("layer_thumbnails", Json::array()))
      layers_->thumbnail(thumbnail);
    history_->clear();
    for (auto row : doc.value("history", Json::array()))
      history_->add(row.get<std::string>().c_str());
    history_->value(history_->size());
    std::string old_path = selected_path();
    channels_->clear();
    channel_names_.clear();
    auto channel = doc.value("channels", Json::object());
    auto composite = channel.value("composite", std::string("RGB"));
    channel_names_.push_back("composite");
    channels_->add(composite.c_str());
    for (auto item : channel.value("colors", Json::array())) {
      auto name = item.value("name", std::string());
      auto key = name;
      std::transform(key.begin(), key.end(), key.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      channel_names_.push_back(key);
      channels_->add(name.c_str());
    }
    for (auto item : channel.value("alpha", Json::array())) {
      auto name = item.value("name", std::string());
      channel_names_.push_back(name);
      channels_->add(name.c_str());
    }
    auto target = channel.value("target", Json::object());
    auto target_kind = target.value("kind", std::string("composite"));
    int channel_row =
        target_kind == "color" ? 2 + target.value("index", 0)
        : target_kind == "alpha"
            ? 2 + int(channel.value("colors", Json::array()).size()) + target.value("index", 0)
            : 1;
    channels_->value(channel_row);
    if (target_kind == "alpha")
      channels_panel_->child(2)->activate();
    else
      channels_panel_->child(2)->deactivate();
    paths_->clear();
    path_names_.clear();
    auto paths = snapshot_.value("view", Json::object()).value("paths", Json::object());
    if (paths.value("workPath", Json()).is_object()) {
      paths_->add("Work Path");
      path_names_.push_back("work");
    }
    for (auto path : paths.value("paths", Json::array())) {
      auto name = path.value("name", std::string());
      paths_->add(name.c_str());
      path_names_.push_back(name);
    }
    if (!path_names_.empty()) {
      auto found = std::find(path_names_.begin(), path_names_.end(), old_path);
      paths_->value(found == path_names_.end() ? 1 : int(found - path_names_.begin()) + 1);
    }
    if (doc.value("hasSelection", false))
      paths_panel_->child(1)->activate();
    else
      paths_panel_->child(1)->deactivate();
    if (!path_names_.empty() && enabled_["path.stroke"])
      paths_panel_->child(2)->activate();
    else
      paths_panel_->child(2)->deactivate();
    std::ostringstream info;
    info << doc.value("width", 0) << " x " << doc.value("height", 0) << " px\n"
         << doc.value("mode", std::string("Rgb")) << " / " << doc.value("depth", 8) << " bit\n"
         << doc.value("resolution", 72.) << " ppi, " << doc.value("layers", Json::array()).size()
         << " layers";
    dimensions_->copy_label(info.str().c_str());
    canvas_->refresh(snapshot_);
    navigator_->redraw();
    histogram_->redraw();
    status();
    frame_->redraw();
    if (!options_.capture.empty())
      app_.request_capture(options_.capture.string());
  }
  void tick() {
    if (options_font_size_ != tool_variant_->textsize()) {
      options_font_size_ = tool_variant_->textsize();
      layout();
    }
    if (job_.valid() && job_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      try {
        auto reply = job_.get();
        snapshot_ = reply.at("snapshot");
        error_ = snapshot_.value("view", Json::object()).value("render_error", std::string());
        busy_ = false;
        if (operation_ == "open" || operation_ == "new" || operation_ == "startup") {
          zoom_ = 0;
          canvas_->reset_pan();
        }
        refresh();
      } catch (const std::exception &e) {
        busy_ = false;
        error_ = e.what();
        status();
      }
    }
    if (!paint_prompt_ && !busy_ && !queued_.empty() && error_.empty()) {
      auto command = std::move(queued_.front());
      queued_.pop_front();
      execute(std::move(command.first), std::move(command.second));
    }
    if (!paint_prompt_ && !busy_ && queued_.empty() && error_.empty() && after_idle_) {
      auto action = std::move(after_idle_);
      after_idle_ = {};
      action();
    }
    if (!paint_prompt_ && !busy_ && queued_.empty() && error_.empty() &&
        layers_panel_->visible_r() && layers_->size() > 0) {
      int first = std::max(0, layers_->topline() - 1);
      std::string key;
      for (int i = first; i < std::min(int(layer_ids_.size()), first + 12); ++i)
        key += std::to_string(layer_ids_[i]) + "/";
      if (key != thumbnail_viewport_) {
        thumbnail_viewport_ = key;
        thumbnail_row_ = first;
        start("layer-preview", [] { return Json::object(); });
      }
    }
    auto now = std::chrono::steady_clock::now();
    if (snapshot_.value("document", Json::object()).value("hasSelection", false) &&
        now >= canvas_->next_selection_frame_) {
      canvas_->selection_phase_ = (canvas_->selection_phase_ + 1) & 7;
      canvas_->next_selection_frame_ = now + std::chrono::milliseconds(100);
      canvas_->redraw();
    }
    if (!options_.report.empty()) {
      auto info = app_.platform_info();
      Json state = {{"application", "photocraft"},
                    {"app_id", info.app_id},
                    {"font_file", info.font_file},
                    {"font", info.font_family},
                    {"renderer", info.renderer},
                    {"menu_service", info.menu_service},
                    {"menu_path", info.menu_path},
                    {"busy", busy_},
                    {"dirty", dirty_},
                    {"path", path_.string()},
                    {"error", error_},
                    {"tool", tool_},
                    {"tool_variant", variant()},
                    {"selection_mode", selection_mode()},
                    {"native_menu_commands", registered_.size()},
                    {"layout", layout_state()},
                    {"snapshot", snapshot_}};
      auto &raster = state["snapshot"]["view"]["raster"];
      if (raster.is_object() && raster.contains("png")) {
        raster["png_bytes"] = raster.at("png").get_ref<const std::string &>().size();
        raster.erase("png");
      }
      if (state["snapshot"]["view"].contains("layer_thumbnails"))
        for (auto &thumbnail : state["snapshot"]["view"]["layer_thumbnails"])
          if (thumbnail.contains("png")) {
            thumbnail["png_bytes"] = thumbnail.at("png").get_ref<const std::string &>().size();
            thumbnail.erase("png");
          }
      auto temp = options_.report.string() + ".tmp";
      {
        std::ofstream out(temp);
        out << state.dump();
      }
      std::filesystem::rename(temp, options_.report);
    }
  }
  static void timer(void *p) {
    auto &self = *static_cast<Photo *>(p);
    try {
      self.tick();
    } catch (const std::exception &e) {
      self.error_ = e.what();
      self.status();
    }
    Fl::repeat_timeout(.033, timer, p);
  }
  static void close_timer(void *p) { static_cast<Photo *>(p)->app_.invoke("app.quit"); }
  Json layout_state() const {
    auto rect = [](const Fl_Widget *w) {
      return Json{{"x", w->x()},
                  {"y", w->y()},
                  {"w", w->w()},
                  {"h", w->h()},
                  {"visible", bool(w->visible())}};
    };
    return {{"style", "macOS Photoshop workspace"},
            {"application_menu", "universal"},
            {"options", rect(options_bar_)},
            {"tools", rect(toolbox_)},
            {"canvas", rect(canvas_)},
            {"documents", rect(documents_)},
            {"dock", rect(dock_)},
            {"color", rect(color_panel_)},
            {"properties", rect(properties_panel_)},
            {"layers", rect(layers_panel_)},
            {"scale", canvas_->scale()},
            {"paper",
             {{"x", canvas_->paper().x},
              {"y", canvas_->paper().y},
              {"w", canvas_->paper().w},
              {"h", canvas_->paper().h}}},
            {"workspace", workspace_->text() ? workspace_->text() : "Essentials"}};
  }

public:
  Photo(Json config, Options options)
      : app_({"org.domed.PhotoCraft", "PhotoCraft", options.width, options.height, options.tray}),
        options_(std::move(options)), config_(std::move(config)) {
    app_.window().size_range(720, 500);
    std::ifstream file(options_.registry.parent_path() /
                       config_.at("catalogue").get<std::string>());
    Json catalogue;
    file >> catalogue;
    for (auto c : catalogue)
      catalogue_[c.at("id")] = c;
    app_.window().begin();
    frame_ = new PhotoFrame(0, app_.content_top(), options_.width,
                            options_.height - app_.content_top(), *this);
    options_bar_ = new Fl_Scroll(0, 0, options_.width, 40);
    options_bar_->type(Fl_Scroll::HORIZONTAL);
    options_bar_->scrollbar_size(8);
    tool_label_ = new Fl_Box(8, 6, 96, 28, "move");
    brush_size_ = new Fl_Value_Input(152, 6, 58, 28, "Size");
    brush_size_->range(.5, 5000);
    brush_size_->value(20);
    brush_size_->step(.5);
    paint_mode_ = new dome_photo::NativeChoice(258, 6, 132, 28, "Mode");
    for (auto mode :
         {"normal", "multiply", "screen", "overlay", "darken", "lighten", "color", "luminosity"})
      paint_mode_->add(mode);
    paint_mode_->value(0);
    brush_opacity_ = new Fl_Value_Input(462, 6, 58, 28, "Opacity");
    brush_opacity_->range(1, 100);
    brush_opacity_->value(100);
    brush_opacity_->step(1);
    hardness_ = new Fl_Value_Input(600, 6, 54, 28, "Hardness");
    hardness_->range(0, 100);
    hardness_->value(80);
    hardness_->step(1);
    workspace_ = new dome_photo::NativeChoice(1000, 6, 190, 28);
    for (auto preset : {"Essentials", "Photography", "Painting", "Graphic and Web"})
      workspace_->add(preset);
    workspace_->value(0);
    bind(workspace_, [this] {
      auto *panel = workspace_->value() == 1   ? histogram_panel_
                    : workspace_->value() == 2 ? color_panel_
                                               : color_panel_;
      top_tabs_->value(panel);
      frame_->redraw();
    });
    tool_variant_ = new dome_photo::NativeChoice(108, 4, 174, 26);
    bind(tool_variant_, [this] {
      tool_variants_[tool_] = tool_variant_->value();
      set_tool(tool_);
    });
    selection_operation_ = new dome_photo::NativeChoice(292, 4, 110, 26);
    for (auto mode : {"New", "Add", "Subtract", "Intersect"})
      selection_operation_->add(mode);
    selection_operation_->value(0);
    selection_operation_->tooltip(
        "Selection mode: Shift adds, Alt subtracts, Shift+Alt intersects");
    feather_ = new Fl_Value_Input(470, 4, 55, 26, "Feather");
    feather_->range(0, 1000);
    feather_->value(0);
    feather_->step(1);
    tolerance_ = new Fl_Value_Input(480, 4, 55, 26, "Tolerance");
    tolerance_->range(0, 255);
    tolerance_->value(32);
    tolerance_->step(1);
    antialias_ = new Fl_Check_Button(546, 4, 110, 26, "Anti-alias");
    antialias_->value(1);
    contiguous_ = new Fl_Check_Button(660, 4, 118, 26, "Contiguous");
    contiguous_->value(1);
    sample_all_ = new Fl_Check_Button(782, 4, 142, 26, "All layers");
    aligned_ = new Fl_Check_Button(686, 4, 112, 26, "Aligned");
    aligned_->value(1);
    type_size_ = new Fl_Value_Input(110, 4, 70, 28, "Size");
    type_size_->range(1, 1296);
    type_size_->value(24);
    type_align_ = new dome_photo::NativeChoice(240, 4, 130, 28, "Align");
    for (auto label : {"Left", "Center", "Right"})
      type_align_->add(label);
    type_align_->value(0);
    type_commit_ = button(390, 4, 100, 28, "Commit", [this] { finish_type(true); });
    type_cancel_ = button(500, 4, 100, 28, "Cancel", [this] { finish_type(false); });
    crop_commit_ = button(110, 4, 100, 28, "Crop", [this] { canvas_->apply_crop(); });
    crop_cancel_ = button(230, 4, 100, 28, "Cancel", [this] { canvas_->cancel_crop(); });
    options_bar_->end();
    options_bar_->remove(workspace_);
    frame_->add(workspace_);
    toolbox_ = new Fl_Group(0, 40, 60, 650);
    const std::vector<std::pair<std::string, std::string>> tools = {
        {"V", "move"},     {"M", "marquee"}, {"L", "lasso"},      {"W", "selection"},
        {"C", "crop"},     {"K", "frame"},   {"I", "eyedropper"}, {"J", "healing"},
        {"B", "brush"},    {"S", "clone"},   {"Y", "history"},    {"E", "eraser"},
        {"G", "gradient"}, {"R", "blur"},    {"O", "dodge"},      {"P", "pen"},
        {"T", "type"},     {"A", "path"},    {"U", "shape"},      {"H", "hand"},
        {"Z", "zoom"}};
    int i = 0;
    for (const auto &[key, name] : tools) {
      auto *b = new ToolButton(3 + (i % 2) * 28, 48 + (i / 2) * 30, 26, 28, name);
      bind(b, [this, name] { set_tool(name); });
      b->copy_label(key.c_str());
      b->copy_tooltip(
          (name + " (" + key + "); right-click for subtools, Shift+letter cycles").c_str());
      b->variants = [this, name, b] { tool_menu(name, b->x() + b->w(), b->y()); };
      b->type(FL_TOGGLE_BUTTON);
      tools_[name] = b;
      ++i;
    }
    auto *pair =
        new ColorPairButton(4, 390, 50, 28, [this] { return std::pair{foreground_, background_}; });
    bind(pair, [this, pair] {
      foreground_dialog(Fl::event_x() >= pair->x() + 22 && Fl::event_y() >= pair->y() + 10);
    });
    color_ = new Fl_Input(4, 424, 52, 26);
    color_->value("#000000");
    color_->textsize(10);
    bind(color_, [this] {
      foreground_ = color();
      execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
    });
    button(4, 456, 24, 24, "D", [this] {
      foreground_ = "#000000";
      background_ = "#ffffff";
      color_->value(foreground_.c_str());
      execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
    });
    button(30, 456, 24, 24, "X", [this] {
      std::swap(foreground_, background_);
      color_->value(foreground_.c_str());
      execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
    });
    button(4, 488, 50, 24, "Mask", [this] { execute("select.editInQuickMaskMode"); });
    toolbox_->end();
    documents_ = new dome_photo::NativeTabs(60, 40, 850, 32);
    documents_->end();
    bind(documents_, [this] {
      if (busy_ || !documents_->value())
        return;
      int index = intptr_t(documents_->value()->user_data()) - 1;
      start("document", [this, index] { return engine_->tool("doc_select", {{"index", index}}); });
    });
    canvas_ = new PhotoCanvas(60, 72, 850, 700, *this);
    rail_ = new Fl_Group(910, 40, 32, 700);
    button(912, 48, 28, 28, "N", [this] { top_tabs_->value(navigator_panel_); });
    button(912, 80, 28, 28, "H", [this] { top_tabs_->value(history_panel_); });
    button(912, 112, 28, 28, "I", [this] { top_tabs_->value(histogram_panel_); });
    button(912, 144, 28, 28, "P", [this] { middle_tabs_->value(properties_panel_); });
    rail_->end();
    dock_ = new Fl_Group(944, 40, 288, 700);
    top_tabs_ = new dome_photo::NativeTabs(944, 40, 288, 190);
    color_panel_ = new Fl_Group(944, 66, 288, 164, "Color");
    auto *palette = new Fl_Box(954, 80, 268, 46, "Foreground / background");
    palette->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    button(954, 136, 124, 28, "Foreground...", [this] { foreground_dialog(); });
    button(1084, 136, 124, 28, "Swap colors", [this] {
      std::swap(foreground_, background_);
      color_->value(foreground_.c_str());
      execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
    });
    for (int n = 0; n < 12; ++n) {
      const std::array<const char *, 12> colors = {"#000000", "#ffffff", "#ff3333", "#ff9933",
                                                   "#ffff33", "#33cc66", "#33cccc", "#3399ff",
                                                   "#3333ff", "#9933ff", "#ff33cc", "#888888"};
      auto value = std::string(colors[n]);
      auto *b = button(954 + (n % 6) * 44, 174 + (n / 6) * 24, 40, 22, "", [this, value] {
        foreground_ = value;
        color_->value(value.c_str());
        execute("tools.setColors", {{"foreground", foreground_}, {"background", background_}});
      });
      unsigned rgb = std::stoul(value.substr(1), nullptr, 16);
      b->color(fl_rgb_color(rgb >> 16, (rgb >> 8) & 255, rgb & 255));
      b->copy_tooltip(value.c_str());
    }
    toolbox_->remove(color_);
    color_panel_->add(color_);
    color_panel_->child(2)->hide();
    color_panel_->end();
    navigator_panel_ = new Fl_Group(944, 66, 288, 164, "Navigator");
    navigator_ = new PhotoNavigator(952, 76, 272, 146, *this);
    navigator_panel_->end();
    histogram_panel_ = new Fl_Group(944, 66, 288, 164, "Histogram");
    histogram_ = new PhotoNavigator(952, 76, 272, 146, *this, true);
    histogram_panel_->end();
    history_panel_ = new Fl_Group(944, 66, 288, 164, "History");
    history_ = new ContextBrowser(952, 76, 272, 146);
    history_->format_char(0);
    bind(history_, [this] {
      if (history_->value() > 0)
        execute("history.selectState", {{"state", history_->value() - 1}});
    });
    history_panel_->end();
    top_tabs_->end();
    middle_tabs_ = new dome_photo::NativeTabs(944, 234, 288, 198);
    properties_panel_ = new Fl_Group(944, 260, 288, 172, "Properties");
    dimensions_ = new Fl_Box(956, 270, 252, 104, "Document properties");
    dimensions_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    button(956, 382, 118, 28, "Image size...", [this] { control("image.imageSize"); });
    button(1080, 382, 128, 28, "Canvas size...", [this] { control("image.canvasSize"); });
    properties_panel_->end();
    auto *adjustments = new Fl_Group(944, 260, 288, 172, "Adjustments");
    std::vector<std::pair<const char *, const char *>> adjustments_list = {
        {"Brightness", "brightnessContrast"},
        {"Levels", "levels"},
        {"Curves", "curves"},
        {"Exposure", "exposure"},
        {"Vibrance", "vibrance"},
        {"Hue / Sat", "hueSaturation"},
        {"Color balance", "colorBalance"},
        {"Black / White", "blackWhite"},
        {"Photo filter", "photoFilter"},
        {"Invert", "invert"},
        {"Posterize", "posterize"},
        {"Threshold", "threshold"}};
    i = 0;
    for (auto [label, id] : adjustments_list) {
      auto command = "layer.newAdjustmentLayer." + std::string(id);
      button(952 + (i % 2) * 136, 270 + (i / 2) * 24, 130, 22, label,
             [this, command] { control(command); });
      ++i;
    }
    adjustments->end();
    middle_tabs_->end();
    bottom_tabs_ = new dome_photo::NativeTabs(944, 436, 288, 330);
    layers_panel_ = new Fl_Group(944, 462, 288, 304, "Layers");
    layer_filter_ = new Fl_Input(952, 468, 264, 26);
    layer_filter_->tooltip("Filter layers by name");
    layer_filter_->when(FL_WHEN_CHANGED);
    bind(layer_filter_, [this] { refresh(); });
    blend_ = new dome_photo::NativeChoice(952, 500, 154, 26);
    for (auto mode : {"Normal", "Dissolve", "Darken", "Multiply", "ColorBurn", "Lighten", "Screen",
                      "ColorDodge", "Overlay", "SoftLight", "HardLight", "Difference", "Hue",
                      "Saturation", "Color", "Luminosity"})
      blend_->add(mode);
    blend_->value(0);
    bind(blend_, [this] { execute("layer.setProps", {{"blend", blend_->text()}}); });
    layer_opacity_ = new Fl_Value_Input(1152, 500, 64, 26, "Opacity");
    layer_opacity_->range(0, 100);
    layer_opacity_->step(1);
    layer_opacity_->value(100);
    bind(layer_opacity_,
         [this] { execute("layer.setProps", {{"opacity", layer_opacity_->value() / 100}}); });
    button(952, 530, 58, 24, "Lock", [this] { execute("layer.setProps", {{"locked", true}}); });
    button(1016, 530, 58, 24, "Unlock", [this] { execute("layer.setProps", {{"locked", false}}); });
    button(1080, 530, 62, 24, "Visible", [this] {
      auto doc = snapshot_.value("document", Json::object());
      auto id = doc.value("activeLayer", 0ul);
      for (auto layer : doc.value("layers", Json::array()))
        if (layer.value("id", 0ul) == id)
          execute("layer.setProps", {{"visible", !layer.value("visible", true)}});
    });
    layers_ = new PhotoLayers(952, 560, 264, 164);
    layers_->format_char(0);
    layers_->visibility = [this](unsigned long id) {
      for (const auto &layer :
           snapshot_.value("document", Json::object()).value("layers", Json::array()))
        if (layer.value("id", 0ul) == id) {
          execute("layer.setProps", {{"layer", id}, {"visible", !layer.value("visible", true)}});
          break;
        }
    };
    bind(layers_, [this] {
      int at = layers_->value() - 1;
      if (at >= 0 && at < int(layer_ids_.size()))
        execute("layer.select", {{"layer", layer_ids_[at]}});
    });
    button(952, 730, 40, 26, "fx", [this] { control("layer.layerStyle.blendingOptions"); });
    button(996, 730, 44, 26, "Mask",
           [this] { control("layer.layerMask.revealAll", Json::object()); });
    button(1044, 730, 44, 26, "Group", [this] { execute("layer.new.group"); });
    button(1092, 730, 44, 26, "New", [this] { execute("layer.new.layer"); });
    button(1140, 730, 74, 26, "Delete", [this] { execute("layer.delete"); });
    layers_panel_->end();
    channels_panel_ = new Fl_Group(944, 462, 288, 304, "Channels");
    channels_ = new ContextBrowser(952, 468, 264, 256);
    channels_->format_char(0);
    bind(channels_, [this] {
      int at = channels_->value() - 1;
      if (at >= 0 && at < int(channel_names_.size()))
        execute("channel.target", {{"channel", channel_names_[at]}});
    });
    button(952, 730, 112, 26, "New channel", [this] { control("channel.new"); });
    button(1072, 730, 112, 26, "Delete", [this] { control("channel.delete"); });
    channels_panel_->end();
    paths_panel_ = new Fl_Group(944, 462, 288, 304, "Paths");
    paths_ = new ContextBrowser(952, 468, 264, 256);
    paths_->format_char(0);
    button(952, 730, 112, 26, "Make path", [this] { execute("select.toWorkPath"); });
    button(1072, 730, 112, 26, "Stroke...", [this] { control("path.stroke"); });
    paths_panel_->end();
    bottom_tabs_->end();
    dock_->end();
    foot_ = new Fl_Group(0, 800, options_.width, 28);
    zoom_choice_ = new dome_photo::NativeChoice(64, 802, 92, 24);
    for (auto percent : {"Fit", "25%", "50%", "100%", "200%", "400%", "800%"})
      zoom_choice_->add(percent);
    zoom_choice_->value(0);
    bind(zoom_choice_, [this] {
      if (zoom_choice_->value() == 0) {
        zoom_ = 0;
        canvas_->reset_pan();
        canvas_->geometry();
        canvas_->redraw();
      } else
        zoom(std::stod(zoom_choice_->text()) / 100);
    });
    status_ = new Fl_Box(172, 800, 900, 28, "Starting engine...");
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    foot_->end();
    frame_->end();
    type_editor_ = new InlineTypeInput(60, 80, 400, 120);
    type_editor_->hide();
    static_cast<InlineTypeInput *>(type_editor_)->finish = [this](bool accept) {
      finish_type(accept);
    };
    context_ = new dome_photo::ContextMenu();
    layers_->context = [this](int row) { layer_menu(row, Fl::event_x(), Fl::event_y()); };
    for (auto pair : std::vector<std::pair<Fl_Hold_Browser *, std::string>>{
             {history_, "history"}, {channels_, "channels"}, {paths_, "paths"}})
      static_cast<ContextBrowser *>(pair.first)->context = [this, name = pair.second](int row) {
        panel_menu(name, row, Fl::event_x(), Fl::event_y());
      };
    app_.window().resizable(frame_);
    app_.window().end();
    app_.style();
    layout();
    set_tool("move");
    for (auto *tabs : {documents_, top_tabs_, middle_tabs_, bottom_tabs_})
      tabs->handle_overflow(Fl_Tabs::OVERFLOW_PULLDOWN);
    setup_menus();
    Fl::add_timeout(.033, timer, this);
    if (options_.lifetime)
      Fl::add_timeout(options_.lifetime / 1000., close_timer, this);
    start("startup", [this] {
      engine_ = std::make_unique<artcraft::UpstreamSession>(config_, options_.engine.string(),
                                                            options_.workspace);
      return options_.document.empty() ? engine_->create() : engine_->open(options_.document);
    });
  }
  ~Photo() {
    Fl::remove_timeout(timer, this);
    Fl::remove_timeout(close_timer, this);
    if (job_.valid())
      job_.wait();
  }
  void setup_menus();
  void layout();
  int run() { return app_.run(); }
};
void Photo::setup_menus() {
  menu("app.about", "&PhotoCraft/&About PhotoCraft", [] {
    fl_message("PhotoCraft for DomeD\nNative image workspace over the pinned "
               "Storytold engine.");
  });
  menu(
      "app.quit", "&PhotoCraft/&Quit",
      [this] {
        if (discard(true))
          app_.quit();
      },
      FL_CTRL + 'q');
  menu("file.new", "&File/&New...", [this] { new_document(); }, FL_CTRL + 'n');
  menu(
      "file.open", "&File/&Open...",
      [this] {
        auto path = choose(false);
        if (!path.empty())
          start("open", [this, path] { return engine_->open(path); });
      },
      FL_CTRL + 'o');
  menu("file.save", "&File/&Save", [this] { save(); }, FL_CTRL + 's');
  menu("file.saveAs", "&File/Save &As...", [this] { save(true); }, FL_CTRL + FL_SHIFT + 's');
  menu("file.exportNative", "&File/&Export image...", [this] {
    auto path = choose(true, ".png");
    if (!path.empty())
      start("export",
            [this, path] { return engine_->tool("doc_export", {{"path", path.string()}}); });
  });
  menu(
      "file.close", "&File/&Close document",
      [this] {
        if (discard())
          start("close", [this] {
            auto r = engine_->tool("doc_close", Json::object());
            auto session = engine_->tool("session_list", Json::object());
            if (session.value("documents", Json::array()).empty())
              engine_->create();
            return r;
          });
      },
      FL_CTRL + 'w');
  menu(
      "app.close", "&File/Close &window",
      [this] {
        if (discard(true))
          app_.quit();
      },
      FL_CTRL + FL_SHIFT + 'w');
  menu("edit.undo", "&Edit/&Undo", [this] { execute("edit.undo"); }, FL_CTRL + 'z', true);
  menu(
      "edit.redo", "&Edit/&Redo", [this] { execute("edit.redo"); }, FL_CTRL + FL_SHIFT + 'z', true);
  menu("select.all", "&Select/&All", [this] { execute("select.all"); }, FL_CTRL + 'a', true);
  menu(
      "select.deselect", "&Select/&Deselect", [this] { execute("select.deselect"); }, FL_CTRL + 'd',
      true);
  menu(
      "layer.new.layer", "&Layer/New/&Layer", [this] { execute("layer.new.layer"); },
      FL_CTRL + FL_SHIFT + 'n', true);
  menu(
      "layer.duplicate", "&Layer/&Duplicate layer", [this] { execute("layer.duplicate"); },
      FL_CTRL + 'j', true);
  menu("layer.delete", "&Layer/&Delete layer", [this] { execute("layer.delete"); }, 0, true);
  if (!options_.report.empty())
    menu("native.brushTest", "&Tools/Paint &foreground dab", [this] {
      auto doc = snapshot_.value("document", Json::object());
      execute(
          "paint.stroke",
          {{"points",
            Json::array({Json::array({doc.value("width", 1) / 2, doc.value("height", 1) / 2, 1})})},
           {"size", brush_size_->value()},
           {"color", color()},
           {"opacity", brush_opacity_->value() / 100}});
    });
  menu(
      "native.fit", "&View/Fit on &screen",
      [this] {
        zoom_ = 0;
        canvas_->reset_pan();
        canvas_->geometry();
        canvas_->redraw();
      },
      FL_CTRL + '0');
  menu("native.actual", "&View/&100%", [this] { zoom(1); }, FL_CTRL + '1');
  menu("native.zoomIn", "&View/Zoom &in", [this] { zoom(canvas_->scale() * 1.25); }, FL_CTRL + '+');
  menu(
      "native.zoomOut", "&View/Zoom &out", [this] { zoom(canvas_->scale() / 1.25); },
      FL_CTRL + '-');
  menu(
      "native.rulers", "&View/&Rulers",
      [this] {
        rulers_ = !rulers_;
        canvas_->geometry();
        canvas_->redraw();
      },
      FL_CTRL + 'r');
  menu(
      "native.panels", "&Window/Show or hide &panels",
      [this] {
        panels_ = !panels_;
        layout();
      },
      FL_Tab);
  if (options_.tray) {
    menu("window.restore", "&Window/&Restore window", [this] { app_.show(); });
    menu("window.hide", "&Window/Hide to &tray", [this] { app_.hide_to_tray(); });
  }
  menu("native.resetWorkspace", "&Window/Workspace/Reset &Essentials", [this] {
    panels_ = true;
    dock_width_ = 288;
    workspace_->value(0);
    top_tabs_->value(color_panel_);
    middle_tabs_->value(properties_panel_);
    bottom_tabs_->value(layers_panel_);
    layout();
  });
  for (auto [label, panel] :
       std::vector<std::pair<const char *, Fl_Group *>>{{"Color", color_panel_},
                                                        {"Navigator", navigator_panel_},
                                                        {"Histogram", histogram_panel_},
                                                        {"History", history_panel_},
                                                        {"Properties", properties_panel_},
                                                        {"Layers", layers_panel_},
                                                        {"Channels", channels_panel_},
                                                        {"Paths", paths_panel_}}) {
    auto id = "native.panel." + std::string(label);
    auto path = "&Window/" + std::string(label);
    menu(id, path, [this, panel] {
      panels_ = true;
      layout();
      if (panel == properties_panel_)
        middle_tabs_->value(panel);
      else if (panel == layers_panel_ || panel == channels_panel_ || panel == paths_panel_)
        bottom_tabs_->value(panel);
      else
        top_tabs_->value(panel);
    });
  }
  menu("native.help", "&Help/PhotoCraft &Help", [] {
    fl_message("Use the toolbox or letter shortcuts to select a tool.\nDrag on "
               "the image to paint, "
               "select, move, crop or draw.\nSpace/Hand pans; Ctrl+wheel "
               "zooms.\nLayer, channel "
               "and history panels edit the upstream document.\nRight-click for contextual "
               "actions; hold a grouped tool for its flyout.");
  });
  std::set<std::string> paths;
  for (const auto &[id, command] : catalogue_) {
    if (registered_.contains(id))
      continue;
    std::string path;
    for (auto item : command.value("menu", Json::array())) {
      auto part = item.get<std::string>();
      for (char &c : part)
        if (c == '/' || c == '&')
          c = ' ';
      path += part + "/";
    }
    if (path.empty())
      continue; // Internal automation commands are not application menu items.
    auto label = command.value("label", id);
    for (char &c : label)
      if (c == '/' || c == '&')
        c = ' ';
    path = "&" + path + label;
    auto original = path;
    int n = 2;
    while (!paths.insert(path).second)
      path = original + " (" + std::to_string(n++) + ")";
    menu(id, path, [this, id] { control(id); }, 0, true);
  }
  const std::vector<std::string> order = {"PhotoCraft", "File",   "Edit",   "Image", "Layer",
                                          "Type",       "Select", "Filter", "3D",    "View",
                                          "Plugins",    "Tools",  "Window", "Help"};
  auto rank = [&](const artcraft::Command &c) {
    auto path = c.menu_path;
    path.erase(std::remove(path.begin(), path.end(), '&'), path.end());
    return std::find(order.begin(), order.end(), path.substr(0, path.find('/'))) - order.begin();
  };
  std::stable_sort(menu_commands_.begin(), menu_commands_.end(),
                   [&](const auto &a, const auto &b) { return rank(a) < rank(b); });
  for (auto &command : menu_commands_)
    app_.add_command(std::move(command));
  menu_commands_.clear();
}
void Photo::layout() {
  const int x = frame_->x(), y = frame_->y(), w = frame_->w(), h = frame_->h(), top = y + 40,
            bottom = y + h - 28;
  int width = w < 1000 ? 240 : dock_width_;
  width = std::clamp(width, 220, std::max(220, w - 360));
  int dx = x + w - width, rail = panels_ ? 32 : 0, right = panels_ ? dx - rail : x + w;
  bool workspace_visible = w >= 1100;
  if (workspace_visible)
    workspace_->show();
  else
    workspace_->hide();
  options_bar_->resize(x, y, w - (workspace_visible ? 200 : 0), 40);
  // Measure the current native bitmap face; fixed 16px assumptions overlap at
  // larger DomeD appearance sizes. Keep every field reachable by horizontal
  // scroll.
  fl_font(tool_variant_->textfont(), tool_variant_->textsize());
  auto text_width = [](const char *text) { return int(std::ceil(fl_width(text ? text : ""))); };
  int offset = x + 6 - options_bar_->xposition();
  for (Fl_Widget *control : std::vector<Fl_Widget *>{
           tool_label_, tool_variant_, selection_operation_, brush_size_, paint_mode_,
           brush_opacity_, hardness_, feather_, tolerance_, antialias_, contiguous_, sample_all_,
           aligned_, type_size_, type_align_, type_commit_, type_cancel_, crop_commit_,
           crop_cancel_}) {
    if (!control->visible())
      continue;
    int width = std::max(60, text_width(control->label()) + 12), label = 0;
    if (auto *choice = dynamic_cast<Fl_Choice *>(control)) {
      width = 70;
      for (int item = 0; item < choice->size() - 1; ++item)
        if (choice->text(item))
          width = std::max(width, text_width(choice->text(item)) + 32);
      label = text_width(control->label());
    } else if (dynamic_cast<Fl_Value_Input *>(control)) {
      width = std::max(58, text_width("1000") + 18);
      label = text_width(control->label());
    } else if (dynamic_cast<Fl_Check_Button *>(control))
      width = text_width(control->label()) + 30;
    if (label)
      offset += label + 8;
    control->resize(offset, y + 3, width, 28);
    offset += width + 12;
  }
  workspace_->resize(x + w - 188, y + 3, 180, 28);
  toolbox_->resize(x, top, 60, bottom - top);
  int cell = std::clamp((bottom - top - 144) / 11, 22, 30);
  for (int i = 0; i < 21 && i < toolbox_->children(); ++i)
    toolbox_->child(i)->resize(x + 3 + (i % 2) * 28, top + 8 + (i / 2) * cell, 26, cell - 2);
  int color_y = top + 11 * cell + 10;
  for (int i = 21; i < toolbox_->children(); ++i) {
    auto *child = toolbox_->child(i);
    std::string label = child->label() ? child->label() : "";
    if (label == "FG")
      child->resize(x + 4, color_y, 50, 26);
    else if (label == "D")
      child->resize(x + 4, color_y + 32, 24, 24);
    else if (label == "X")
      child->resize(x + 30, color_y + 32, 24, 24);
    else if (label == "Mask")
      child->resize(x + 4, color_y + 62, 50, 24);
  }

  documents_->resize(x + 60, top, std::max(100, right - x - 60), 32);
  for (int i = 0; i < documents_->children(); ++i)
    documents_->child(i)->resize(documents_->x(), top + 30, documents_->w(), 1);
  canvas_->resize(x + 60, top + 32, std::max(100, right - x - 60),
                  std::max(100, bottom - top - 32));
  if (panels_) {
    dock_->show();
    rail_->show();
  } else {
    dock_->hide();
    rail_->hide();
  }
  rail_->resize(dx - 32, top, 32, bottom - top);
  for (int i = 0; i < rail_->children(); ++i)
    rail_->child(i)->resize(dx - 30, top + 8 + i * 32, 28, 28);
  dock_->resize(dx, top, width, bottom - top);
  int avail = bottom - top, ch = std::min(color_height_, std::max(112, avail - 120 - 192)),
      ph = std::min(properties_height_, std::max(120, avail - ch - 192)), ly = top + ch + ph + 8,
      lh = std::max(130, bottom - ly);
  top_tabs_->resize(dx, top, width, ch);
  for (auto *panel : {color_panel_, navigator_panel_, histogram_panel_, history_panel_})
    panel->resize(dx, top + 26, width, ch - 26);
  bool small = ch < 160;
  auto *description = color_panel_->child(0);
  if (small)
    description->hide();
  else
    description->show();
  description->resize(dx + 10, top + 34, width - 20, 28);
  int cy = top + (small ? 32 : 66);
  color_panel_->child(1)->copy_label("FG...");
  color_panel_->child(1)->tooltip("Edit foreground colour");
  color_panel_->child(1)->resize(dx + 8, cy, (width - 24) / 2, 24);
  color_panel_->child(2)->hide();
  color_->resize(dx + 16 + (width - 24) / 2, cy, (width - 24) / 2, 24);
  for (int n = 0; n < 12; ++n)
    color_panel_->child(n + 3)->resize(dx + 8 + (n % 6) * (width - 16) / 6, cy + 30 + (n / 6) * 24,
                                       (width - 24) / 6, 22);
  navigator_->resize(dx + 8, top + 34, width - 16, ch - 42);
  histogram_->resize(dx + 8, top + 34, width - 16, ch - 42);
  history_->resize(dx + 8, top + 34, width - 16, ch - 42);
  middle_tabs_->resize(dx, top + ch + 4, width, ph);
  for (int i = 0; i < middle_tabs_->children(); ++i)
    middle_tabs_->child(i)->resize(dx, top + ch + 30, width, ph - 26);
  dimensions_->resize(dx + 10, top + ch + 36, width - 20, std::max(42, ph - 72));
  properties_panel_->child(1)->copy_label("Image");
  properties_panel_->child(1)->tooltip("Image size and resolution");
  properties_panel_->child(2)->copy_label("Canvas");
  properties_panel_->child(2)->tooltip("Canvas size");
  properties_panel_->child(1)->resize(dx + 8, top + ch + ph - 28, (width - 24) / 2, 26);
  properties_panel_->child(2)->resize(dx + 16 + (width - 24) / 2, top + ch + ph - 28,
                                      (width - 24) / 2, 26);
  auto *adjustments = dynamic_cast<Fl_Group *>(middle_tabs_->child(1));
  if (adjustments)
    for (int i = 0; i < adjustments->children(); ++i)
      adjustments->child(i)->resize(dx + 8 + (i % 2) * (width - 16) / 2,
                                    top + ch + 34 + (i / 2) * std::max(14, (ph - 36) / 6),
                                    (width - 24) / 2, std::max(14, (ph - 42) / 6));
  bottom_tabs_->resize(dx, ly, width, lh);
  for (auto *panel : {layers_panel_, channels_panel_, paths_panel_})
    panel->resize(dx, ly + 26, width, lh - 26);
  layer_filter_->resize(dx + 8, ly + 32, width - 16, 24);
  blend_->resize(dx + 8, ly + 60, std::max(90, width - 126), 24);
  layer_opacity_->resize(dx + width - 62, ly + 60, 54, 24);
  layer_opacity_->label("");
  layer_opacity_->tooltip("Layer opacity (%)");
  bool locks = lh > 230;
  for (int i = 3; i < 6; ++i) {
    auto *child = layers_panel_->child(i);
    if (locks) {
      child->show();
      child->resize(dx + 8 + (i - 3) * 68, ly + 88, 62, 24);
    } else
      child->hide();
  }
  int list_y = ly + (locks ? 118 : 88);
  layers_->resize(dx + 8, list_y, width - 16, std::max(28, bottom - list_y - 36));
  int button_width = (width - 16) / 5;
  const char *icons[] = {"fx", "M", "G", "+", "-"};
  const char *tips[] = {"Layer effects", "Add layer mask", "New group", "New layer",
                        "Delete layer"};
  for (int i = 7; i < 12; ++i) {
    auto *control = layers_panel_->child(i);
    control->copy_label(icons[i - 7]);
    control->tooltip(tips[i - 7]);
    control->resize(dx + 8 + (i - 7) * button_width, bottom - 30, button_width - 4, 26);
  }
  channels_->resize(dx + 8, ly + 34, width - 16, std::max(50, lh - 72));
  paths_->resize(dx + 8, ly + 34, width - 16, std::max(50, lh - 72));
  for (auto *panel : {channels_panel_, paths_panel_})
    for (int i = 1; i < panel->children(); ++i)
      panel->child(i)->resize(dx + 8 + (i - 1) * (width - 16) / 2, bottom - 30, (width - 24) / 2,
                              26);
  foot_->resize(x, bottom, w, 28);
  zoom_choice_->resize(x + 64, bottom + 2, 92, 24);
  status_->resize(x + 168, bottom, std::max(1, w - 180), 28);
  canvas_->geometry();
  frame_->redraw();
  if (!busy_ && !options_.capture.empty())
    app_.request_capture(options_.capture.string());
}
void PhotoFrame::resize(int x, int y, int w, int h) {
  Fl_Widget::resize(x, y, w, h);
  owner_.layout();
}
int PhotoFrame::handle(int event) {
  if (owner_.context_->visible() && event == FL_PUSH &&
      !owner_.context_->contains(Fl::event_x(), Fl::event_y())) {
    owner_.context_->close();
    return 1;
  }

  if (event == FL_PUSH && owner_.panels_) {
    int mx = Fl::event_x(), my = Fl::event_y();
    if (std::abs(mx - owner_.dock_->x() + 32) < 5) {
      grip_ = 1;
      anchor_ = mx;
      return 1;
    }
    if (mx > owner_.dock_->x() &&
        std::abs(my - owner_.top_tabs_->y() - owner_.top_tabs_->h()) < 5) {
      grip_ = 2;
      anchor_ = my;
      return 1;
    }
    if (mx > owner_.dock_->x() &&
        std::abs(my - owner_.middle_tabs_->y() - owner_.middle_tabs_->h()) < 5) {
      grip_ = 3;
      anchor_ = my;
      return 1;
    }
  }
  if ((event == FL_DRAG || event == FL_RELEASE) && grip_) {
    int at = grip_ == 1 ? Fl::event_x() : Fl::event_y();
    int delta = at - anchor_;
    anchor_ = at;
    if (grip_ == 1)
      owner_.dock_width_ = std::clamp(owner_.dock_width_ - delta, 220, 480);
    else if (grip_ == 2)
      owner_.color_height_ = std::clamp(owner_.color_height_ + delta, 112, 400);
    else
      owner_.properties_height_ = std::clamp(owner_.properties_height_ + delta, 120, 400);
    owner_.layout();
    if (event == FL_RELEASE)
      grip_ = 0;
    return 1;
  }
  return Fl_Group::handle(event);
}
bool PhotoCanvas::selected(double px, double py) const {
  auto preview =
      owner_.snapshot_.value("document", Json::object()).value("selectionPreview", Json());
  if (!preview.is_object())
    return false;
  auto origin = preview.at("origin");
  int step = preview.at("step"), column = int(std::floor((px - origin[0].get<int>()) / step)),
      row = int(std::floor((py - origin[1].get<int>()) / step));
  const auto &rows = preview.at("rows");
  if (row < 0 || row >= int(rows.size()) || column < 0)
    return false;
  const auto &line = rows[row].get_ref<const std::string &>();
  return column < int(line.size()) && line[column] == '1';
}
void PhotoCanvas::finish_polygon() {
  if (points_.size() < 3)
    return;
  Json polygon = Json::array();
  for (auto &point : points_)
    polygon.push_back(Json::array({point[0], point[1]}));
  points_ = Json::array();
  owner_.execute("select.lasso", {{"points", polygon},
                                  {"mode", gesture_mode_},
                                  {"polygonal", true},
                                  {"antiAlias", bool(owner_.antialias_->value())},
                                  {"feather", owner_.feather_->value()}});
  redraw();
}
int PhotoCanvas::ruler_size() const {
  return owner_.rulers_ ? std::max(22, owner_.app_.platform_info().font_pixels * 2) : 0;
}
void PhotoCanvas::apply_crop() {
  if (!crop_.is_array())
    return;
  auto rectangle = crop_;
  crop_ = Json();
  owner_.zoom_ = 0;
  reset_pan();
  owner_.execute("image.crop", {{"x", rectangle[0]},
                                {"y", rectangle[1]},
                                {"width", rectangle[2]},
                                {"height", rectangle[3]}});
}
void PhotoCanvas::finish_path(bool closed) {
  if (points_.size() < 2)
    return;
  Json knots = Json::array();
  for (auto point : points_)
    knots.push_back(Json::array({point[0], point[1]}));
  points_ = Json::array();
  owner_.execute(
      "path.set",
      {{"name", "work"},
       {"path", {{"subpaths", Json::array({Json{{"closed", closed}, {"knots", knots}}})}}}});
  redraw();
}
void PhotoCanvas::geometry() {
  const int inset = ruler_size();
  auto doc = owner_.snapshot_.value("document", Json::object());
  int width = std::max(1, doc.value("width", 1)), height = std::max(1, doc.value("height", 1));
  scale_ = owner_.zoom_ > 0
               ? owner_.zoom_
               : std::min(double(w() - inset - 40) / width, double(h() - inset - 40) / height);
  scale_ = std::clamp(scale_, .005, 32.);
  int rw = std::max(1, int(width * scale_)), rh = std::max(1, int(height * scale_));
  paper_ = {x() + inset + (w() - inset - rw) / 2 + int(pan_x_),
            y() + inset + (h() - inset - rh) / 2 + int(pan_y_), rw, rh};
  if (owner_.zoom_choice_) {
    auto percent = std::to_string(int(std::round(scale_ * 100))) + "%";
    int index = owner_.zoom_choice_->find_index(percent.c_str());
    if (index < 0)
      index = owner_.zoom_choice_->add(percent.c_str());
    owner_.zoom_choice_->value(index);
  }
}
void PhotoCanvas::refresh(const Json &state) {
  if (state.value("view", Json::object()).contains("render_error")) {
    image_.clear();
    bins_.fill(0);
    geometry();
    redraw();
    return;
  }
  auto png = state.value("view", Json::object())
                 .value("raster", Json::object())
                 .value("png", std::string());
  if (!png.empty() && image_.set(png)) {
    bins_.fill(0);
    auto *image = image_.pixels();
    if (image && image->d() >= 3) {
      const int stride = image->ld() ? image->ld() : image->data_w() * image->d();
      for (int y = 0; y < image->data_h(); ++y)
        for (int x = 0; x < image->data_w(); ++x) {
          const auto *pixel = image->array + size_t(y) * stride + x * image->d();
          if (image->d() == 4 && pixel[3] == 0)
            continue;
          ++bins_[(54u * pixel[0] + 183u * pixel[1] + 19u * pixel[2]) >> 8];
        }
    }
  }
  geometry();
  redraw();
}
void PhotoCanvas::draw() {
  fl_push_clip(x(), y(), w(), h());
  fl_color(fl_darker(FL_BACKGROUND_COLOR));
  fl_rectf(x(), y(), w(), h());
  geometry();
  int inset = ruler_size();
  ScreenRect clip{x() + inset, y() + inset, w() - inset, h() - inset};
  if (!image_.empty()) {
    int left = std::max(paper_.x, clip.x), top = std::max(paper_.y, clip.y),
        right = std::min(paper_.x + paper_.w, clip.x + clip.w),
        bottom = std::min(paper_.y + paper_.h, clip.y + clip.h);
    for (int cy = top; cy < bottom; cy += 12)
      for (int cx = left; cx < right; cx += 12) {
        fl_color(((cx - left) / 12 + (cy - top) / 12) % 2 ? fl_rgb_color(175, 175, 175)
                                                          : fl_rgb_color(215, 215, 215));
        fl_rectf(cx, cy, std::min(12, right - cx), std::min(12, bottom - cy));
      }
    image_.draw(*this, paper_, clip);
    fl_color(FL_DARK3);
    fl_rect(paper_.x - 1, paper_.y - 1, paper_.w + 2, paper_.h + 2);
  }
  auto preview =
      owner_.snapshot_.value("document", Json::object()).value("selectionPreview", Json());
  if (preview.is_object()) {
    const auto &rows = preview.at("rows");
    int step = preview.at("step"), ox = preview["origin"][0], oy = preview["origin"][1];
    if (moving_selection_ && points_.size() > 1) {
      ox += int(points_.back()[0].get<double>() - points_.front()[0].get<double>());
      oy += int(points_.back()[1].get<double>() - points_.front()[1].get<double>());
    }
    auto filled = [&](int column, int row) {
      if (row < 0 || row >= int(rows.size()) || column < 0)
        return false;
      const auto &line = rows[row].get_ref<const std::string &>();
      return column < int(line.size()) && line[column] == '1';
    };
    auto edge = [&](int x1, int y1, int x2, int y2) {
      int left = paper_.x + int(x1 * scale_), top = paper_.y + int(y1 * scale_),
          right = paper_.x + int(x2 * scale_), bottom = paper_.y + int(y2 * scale_);
      // Keep the outer image boundary on its final visible pixel.
      if (left == paper_.x + paper_.w)
        --left;
      if (right == paper_.x + paper_.w)
        --right;
      if (top == paper_.y + paper_.h)
        --top;
      if (bottom == paper_.y + paper_.h)
        --bottom;
      bool horizontal = top == bottom;
      int fixed = horizontal ? top : left;
      if (fixed < (horizontal ? clip.y : clip.x) ||
          fixed >= (horizontal ? clip.y + clip.h : clip.x + clip.w))
        return;
      int begin = std::max(horizontal ? std::min(left, right) : std::min(top, bottom),
                           horizontal ? clip.x : clip.y);
      int end = std::min(horizontal ? std::max(left, right) : std::max(top, bottom),
                         horizontal ? clip.x + clip.w - 1 : clip.y + clip.h - 1);
      // Axis-aligned FLTK GPU lines use filled rectangles, so GL line stipple
      // cannot produce dashes. Explicit pixel bands retain contrast and a
      // continuous phase even when the contour arrives in one-cell segments.
      for (int at = begin; at <= end;) {
        int phase = (at + fixed + selection_phase_) & 7;
        int length = std::min(end - at + 1, 4 - (phase & 3));
        fl_color(phase < 4 ? FL_BLACK : FL_WHITE);
        if (horizontal)
          fl_rectf(at, fixed, length, 1);
        else
          fl_rectf(fixed, at, 1, length);
        at += length;
      }
    };
    int width = preview.at("width"), height = preview.at("height");
    dome_office::GpuClip selection_clip(*this, clip);
    for (int row = 0; row < int(rows.size()); ++row)
      for (int column = 0; column < int(rows[row].get_ref<const std::string &>().size());
           ++column) {
        if (!filled(column, row))
          continue;
        int left = ox + column * step, top = oy + row * step,
            right = ox + std::min(width, (column + 1) * step),
            bottom = oy + std::min(height, (row + 1) * step);
        if (!filled(column - 1, row))
          edge(left, top, left, bottom);
        if (!filled(column + 1, row))
          edge(right, top, right, bottom);
        if (!filled(column, row - 1))
          edge(left, top, right, top);
        if (!filled(column, row + 1))
          edge(left, bottom, right, bottom);
      }
    fl_line_style(0);
  }
  if (crop_.is_array()) {
    int left = paper_.x + int(crop_[0].get<double>() * scale_),
        top = paper_.y + int(crop_[1].get<double>() * scale_),
        width = int(crop_[2].get<double>() * scale_), height = int(crop_[3].get<double>() * scale_);
    fl_color(FL_WHITE);
    fl_line_style(FL_SOLID, 1);
    fl_rect(left, top, width, height);
    for (int i = 1; i < 3; ++i) {
      fl_line(left + i * width / 3, top, left + i * width / 3, top + height);
      fl_line(left, top + i * height / 3, left + width, top + i * height / 3);
    }
    fl_line_style(0);
  }
  if (points_.size() > 1 && !panning_ && !moving_selection_) {
    fl_color(FL_SELECTION_COLOR);
    auto first = points_.front(), last = points_.back();
    int sx = paper_.x + int(first[0].get<double>() * scale_),
        sy = paper_.y + int(first[1].get<double>() * scale_),
        ex = paper_.x + int(last[0].get<double>() * scale_),
        ey = paper_.y + int(last[1].get<double>() * scale_);
    if (owner_.tool_ == "marquee" || owner_.tool_ == "crop" || owner_.tool_ == "shape" ||
        owner_.tool_ == "frame") {
      fl_line_style(FL_DASH, 1);
      if ((owner_.tool_ == "marquee" && gesture_variant_ == 1) ||
          (owner_.tool_ == "shape" && gesture_variant_ == 1))
        fl_arc(std::min(sx, ex), std::min(sy, ey), std::abs(ex - sx), std::abs(ey - sy), 0, 360);
      else
        fl_rect(std::min(sx, ex), std::min(sy, ey), std::abs(ex - sx), std::abs(ey - sy));
      fl_line_style(0);
    } else {
      bool line_tool = owner_.tool_ == "pen" || owner_.tool_ == "lasso" || owner_.tool_ == "move" ||
                       owner_.tool_ == "gradient" || moving_selection_;
      fl_line_style(FL_SOLID,
                    line_tool ? 1 : std::max(1, int(owner_.brush_size_->value() * scale_)));
      for (size_t i = 1; i < points_.size(); ++i)
        fl_line(paper_.x + int(points_[i - 1][0].get<double>() * scale_),
                paper_.y + int(points_[i - 1][1].get<double>() * scale_),
                paper_.x + int(points_[i][0].get<double>() * scale_),
                paper_.y + int(points_[i][1].get<double>() * scale_));
      fl_line_style(0);
    }
  }
  if (owner_.rulers_) {
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x(), y(), w(), inset);
    fl_rectf(x(), y(), inset, h());
    fl_color(FL_FOREGROUND_COLOR);
    fl_font(labelfont(), labelsize());
    double step = 100;
    while (step * scale_ < 50)
      step *= 2;
    while (step * scale_ > 200 && step > 1)
      step /= 2;
    auto doc = owner_.snapshot_.value("document", Json::object());
    for (double v = 0; v <= doc.value("width", 1) && v / step < 300; v += step) {
      int px = paper_.x + int(v * scale_);
      if (px < x() + inset || px > x() + w())
        continue;
      fl_line(px, y() + inset - 6, px, y() + inset - 1);
      auto label = std::to_string(int(v));
      fl_draw(label.c_str(), px + 3, y() + inset - 8);
    }
    for (double v = 0; v <= doc.value("height", 1) && v / step < 300; v += step) {
      int py = paper_.y + int(v * scale_);
      if (py < y() + inset || py > y() + h())
        continue;
      fl_line(x() + inset - 6, py, x() + inset - 1, py);
      auto label = std::to_string(int(v));
      fl_draw(label.c_str(), x() + 1, py - 3);
    }
  }
  fl_pop_clip();
}
int PhotoCanvas::handle(int event) {
  try {
    if (event == FL_FOCUS || event == FL_UNFOCUS)
      return 1;
    if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE)
      return 1;
    if (event == FL_RELEASE && Fl::event_button() == FL_RIGHT_MOUSE) {
      owner_.canvas_menu(Fl::event_x(), Fl::event_y());
      return 1;
    }
    if (event == FL_RELEASE && points_.empty())
      return 1;
    if (event == FL_ENTER || event == FL_MOVE) {
      owner_.app_.window().cursor(owner_.tool_ == "hand" ? FL_CURSOR_HAND : FL_CURSOR_CROSS);
      return 1;
    }
    if (event == FL_LEAVE) {
      owner_.app_.window().cursor(FL_CURSOR_DEFAULT);
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Enter && owner_.tool_ == "crop") {
      apply_crop();
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
      cancel_crop();
      points_ = Json::array();
      panning_ = moving_selection_ = false;
      redraw();
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Enter && owner_.tool_ == "pen") {
      finish_path(false);
      return 1;
    }
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Enter && owner_.tool_ == "lasso" &&
        owner_.variant() == 1) {
      finish_polygon();
      return 1;
    }
    if (event == FL_MOUSEWHEEL) {
      if (Fl::event_state(FL_CTRL)) {
        owner_.zoom(scale_ * (Fl::event_dy() > 0 ? .8 : 1.25));
      } else {
        pan_y_ -= Fl::event_dy() * 32;
        pan_x_ -= Fl::event_dx() * 32;
        geometry();
        redraw();
      }
      return 1;
    }
    if (event == FL_KEYDOWN && !Fl::event_state(FL_CTRL | FL_ALT | FL_META)) {
      int key = std::tolower(Fl::event_key());
      const std::map<int, std::string> tools = {
          {'v', "move"},     {'m', "marquee"}, {'l', "lasso"},      {'w', "selection"},
          {'c', "crop"},     {'k', "frame"},   {'i', "eyedropper"}, {'j', "healing"},
          {'b', "brush"},    {'s', "clone"},   {'y', "history"},    {'e', "eraser"},
          {'g', "gradient"}, {'r', "blur"},    {'o', "dodge"},      {'p', "pen"},
          {'t', "type"},     {'a', "path"},    {'u', "shape"},      {'h', "hand"},
          {'z', "zoom"}};
      if (auto t = tools.find(key); t != tools.end()) {
        if (Fl::event_state(FL_SHIFT) && owner_.tool_ == t->second) {
          auto choices = owner_.variants(t->second);
          if (!choices.empty())
            owner_.tool_variants_[t->second] = (owner_.variant() + 1) % choices.size();
        }
        owner_.set_tool(t->second);
        return 1;
      }
      if (key == 'd' || key == 'x') {
        if (key == 'd') {
          owner_.foreground_ = "#000000";
          owner_.background_ = "#ffffff";
        } else
          std::swap(owner_.foreground_, owner_.background_);
        owner_.color_->value(owner_.foreground_.c_str());
        owner_.execute("tools.setColors",
                       {{"foreground", owner_.foreground_}, {"background", owner_.background_}});
        return 1;
      }
      if (key == '[' || key == ']') {
        owner_.brush_size_->value(
            std::clamp(owner_.brush_size_->value() * (key == '[' ? .8 : 1.25), .5, 5000.));
        return 1;
      }
      if (key == FL_Tab) {
        owner_.panels_ = !owner_.panels_;
        owner_.layout();
        return 1;
      }
      if ((key == FL_Delete || key == FL_BackSpace) && owner_.tool_ == "pen" && !points_.empty()) {
        points_.erase(points_.end() - 1);
        redraw();
        return 1;
      }
      if (key == FL_Delete || key == FL_BackSpace) {
        owner_.execute("edit.clear");
        return 1;
      }
    }
    if (owner_.busy_ && (owner_.operation_ == "startup" || owner_.operation_ == "open" ||
                         owner_.operation_ == "document" || owner_.operation_ == "close" ||
                         owner_.operation_ == "new"))
      return 1;
    auto doc = owner_.snapshot_.value("document", Json::object());
    int width = std::max(1, doc.value("width", 1)), height = std::max(1, doc.value("height", 1));
    geometry();
    double px = (Fl::event_x() - paper_.x) / scale_, py = (Fl::event_y() - paper_.y) / scale_;
    bool inside = px >= 0 && py >= 0 && px < double(width) && py < double(height) &&
                  Fl::event_x() >= x() + ruler_size() && Fl::event_y() >= y() + ruler_size();
    bool selecting = owner_.tool_ == "marquee" || owner_.tool_ == "lasso";
    px = std::clamp(px, 0., double(selecting ? width : width - 1));
    py = std::clamp(py, 0., double(selecting ? height : height - 1));
    if (event == FL_PUSH) {
      take_focus();
      if (Fl::event_button() != FL_LEFT_MOUSE && Fl::event_button() != FL_MIDDLE_MOUSE)
        return 1;
      bool pan =
          owner_.tool_ == "hand" || Fl::event_button() == FL_MIDDLE_MOUSE || Fl::event_key(' ');
      if (!pan && !inside)
        return 1;
      if (owner_.tool_ == "pen" && !pan) {
        if (points_.size() >= 3 && std::hypot(px - points_.front()[0].get<double>(),
                                              py - points_.front()[1].get<double>()) *
                                           scale_ <
                                       8) {
          finish_path(true);
          return 1;
        }
        if (points_.size() < 8192)
          points_.push_back(Json::array({px, py, 1.}));
        if (Fl::event_clicks() && points_.size() >= 2)
          finish_path(false);
        redraw();
        return 1;
      }
      if (owner_.tool_ == "lasso" && owner_.variant() == 1 && !pan) {
        if (points_.empty()) {
          gesture_mode_ = owner_.selection_mode();
          gesture_tool_ = owner_.tool_;
          gesture_variant_ = 1;
        }
        if (points_.size() < 8192)
          points_.push_back(Json::array({px, py, 1.}));
        if (Fl::event_clicks() && points_.size() >= 3)
          finish_polygon();
        redraw();
        return 1;
      }
      if (!pan && owner_.tool_ == "brush" && Fl::event_state(FL_ALT)) {
        points_ = Json::array();
        owner_.execute("native.sampleColor",
                       {{"x", px}, {"y", py}, {"size", 1}, {"sampleLayer", "all"}});
        return 1;
      }
      gesture_tool_ = owner_.tool_;
      gesture_mode_ = owner_.selection_mode();
      gesture_variant_ = owner_.variant();
      moving_selection_ = selecting && gesture_mode_ == "replace" && selected(px, py);
      if (owner_.tool_ == "clone" && Fl::event_state(FL_ALT)) {
        clone_source_ = Json::array({px, py});
        clone_offset_ = Json();
        owner_.error_ = "Clone source set";
        owner_.status();
        return 1;
      }
      panning_ = pan;
      start_x_ = Fl::event_x();
      start_y_ = Fl::event_y();
      points_ = Json::array({Json::array({px, py, 1.})});
      redraw();
      if (owner_.tool_ == "zoom") {
        owner_.zoom(scale_ * (Fl::event_state(FL_ALT) ? .8 : 1.25));
        points_ = Json::array();
        return 1;
      }
      return 1;
    }
    if ((event == FL_DRAG || event == FL_RELEASE) && !points_.empty()) {
      if (owner_.tool_ == "pen" && !panning_)
        return 1;
      if (owner_.tool_ == "lasso" && owner_.variant() == 1 && !panning_)
        return 1;
      if (panning_) {
        pan_x_ += Fl::event_x() - start_x_;
        pan_y_ += Fl::event_y() - start_y_;
        start_x_ = Fl::event_x();
        start_y_ = Fl::event_y();
        geometry();
        if (event == FL_RELEASE)
          points_ = Json::array();
        redraw();
        return 1;
      }
      if (points_.size() < 8192)
        points_.push_back(Json::array({px, py, 1.}));
      if (event == FL_DRAG) {
        redraw();
        return 1;
      }
      auto points = std::move(points_);
      points_ = Json::array();
      auto first = points.front(), last = points.back();
      int x0 = int(std::min(first[0].get<double>(), last[0].get<double>())),
          y0 = int(std::min(first[1].get<double>(), last[1].get<double>())),
          rw = std::max(1, int(std::abs(first[0].get<double>() - last[0].get<double>()))),
          rh = std::max(1, int(std::abs(first[1].get<double>() - last[1].get<double>())));
      auto tool = gesture_tool_;
      if (moving_selection_) {
        moving_selection_ = false;
        int dx = int(std::round(last[0].get<double>() - first[0].get<double>())),
            dy = int(std::round(last[1].get<double>() - first[1].get<double>()));
        if (dx || dy)
          owner_.execute("select.transformSelection",
                         {{"dx", dx}, {"dy", dy}, {"stepName", "Move Selection"}});
        else
          owner_.execute("select.deselect");
      } else if (tool == "move" && doc.value("hasSelection", false))
        owner_.execute("select.float", {{"dx", int(last[0].get<double>() - first[0].get<double>())},
                                        {"dy", int(last[1].get<double>() - first[1].get<double>())},
                                        {"copy", bool(Fl::event_state(FL_ALT))}});
      else if (tool == "move")
        owner_.execute("layer.translate",
                       {{"dx", int(last[0].get<double>() - first[0].get<double>())},
                        {"dy", int(last[1].get<double>() - first[1].get<double>())}});
      else if (tool == "marquee") {
        if (gesture_variant_ == 2) {
          x0 = 0;
          rw = width;
          rh = 1;
        }
        if (gesture_variant_ == 3) {
          y0 = 0;
          rh = height;
          rw = 1;
        }
        if (std::abs(last[0].get<double>() - first[0].get<double>()) < .5 &&
            std::abs(last[1].get<double>() - first[1].get<double>()) < .5 && gesture_variant_ < 2) {
          if (gesture_mode_ == "replace")
            owner_.execute("select.deselect");
        } else
          owner_.execute("select.rect", {{"x", x0},
                                         {"y", y0},
                                         {"width", rw},
                                         {"height", rh},
                                         {"mode", gesture_mode_},
                                         {"ellipse", gesture_variant_ == 1},
                                         {"antiAlias", bool(owner_.antialias_->value())},
                                         {"feather", owner_.feather_->value()}});
      } else if (tool == "lasso") {
        Json polygon = Json::array();
        for (const auto &point : points)
          polygon.push_back(Json::array({point[0], point[1]}));
        if (polygon.size() >= 3)
          owner_.execute("select.lasso", {{"points", polygon},
                                          {"mode", gesture_mode_},
                                          {"antiAlias", bool(owner_.antialias_->value())},
                                          {"feather", owner_.feather_->value()}});
        else if (gesture_mode_ == "replace")
          owner_.execute("select.deselect");
      } else if (tool == "selection") {
        if (gesture_variant_ == 1) {
          Json stroke = Json::array();
          for (auto &point : points)
            stroke.push_back(Json::array({point[0], point[1]}));
          if (gesture_mode_ == "intersect") {
            owner_.error_ = "Quick selection supports New, Add or Subtract";
            owner_.status();
          } else
            owner_.execute("select.quick",
                           {{"points", stroke},
                            {"size", owner_.brush_size_->value()},
                            {"mode", gesture_mode_},
                            {"sampleAllLayers", bool(owner_.sample_all_->value())}});
        } else
          owner_.execute("select.magicWand",
                         {{"x", int(first[0].get<double>())},
                          {"y", int(first[1].get<double>())},
                          {"mode", gesture_mode_},
                          {"tolerance", owner_.tolerance_->value()},
                          {"contiguous", bool(owner_.contiguous_->value())},
                          {"antiAlias", bool(owner_.antialias_->value())},
                          {"sampleAllLayers", bool(owner_.sample_all_->value())}});
      } else if (tool == "crop" && rw >= 2 && rh >= 2) {
        crop_ = Json::array({x0, y0, rw, rh});
        redraw();
      } else if (tool == "frame")
        owner_.execute("layer.new.frame", {{"x", x0}, {"y", y0}, {"width", rw}, {"height", rh}});
      else if (tool == "shape")
        owner_.execute("shape.create", {{"kind", gesture_variant_ == 1 ? "ellipse" : "rect"},
                                        {"rect", Json::array({x0, y0, rw, rh})},
                                        {"fill", owner_.color()}});
      else if (tool == "type") {
        owner_.begin_type(first[0], first[1]);
      } else if (tool == "eyedropper")
        owner_.execute("native.sampleColor",
                       {{"x", px}, {"y", py}, {"size", 1}, {"sampleLayer", "all"}});
      else if (tool == "gradient")
        owner_.execute(
            "paint.gradient",
            {{"from", Json::array({first[0], first[1]})},
             {"to", Json::array({last[0], last[1]})},
             {"colors", Json::array({owner_.color(), owner_.background_})},
             {"style", std::array<const char *, 5>{"linear", "radial", "angle", "reflected",
                                                   "diamond"}[std::clamp(gesture_variant_, 0, 4)]},
             {"opacity", owner_.brush_opacity_->value()},
             {"mode", owner_.paint_mode_->text()}});
      else if (tool == "path")
        owner_.execute("path.transform",
                       {{"name", owner_.selected_path()},
                        {"translateX", last[0].get<double>() - first[0].get<double>()},
                        {"translateY", last[1].get<double>() - first[1].get<double>()}});
      else {
        std::string command = tool == "healing"   ? "paint.spotHealing"
                              : tool == "clone"   ? "paint.cloneStamp"
                              : tool == "history" ? "paint.historyBrush"
                              : tool == "blur"    ? "paint.blur"
                              : tool == "dodge"   ? "paint.dodge"
                                                  : "paint.stroke";
        Json params = {{"points", points},
                       {"size", owner_.brush_size_->value()},
                       {"color", owner_.color()},
                       {"mode", owner_.paint_mode_->text()}};
        bool normal = command == "paint.stroke";
        params["opacity"] = owner_.brush_opacity_->value() / (normal ? 100. : 1.);
        params["hardness"] = owner_.hardness_->value() / (normal ? 100. : 1.);
        if (tool == "eraser")
          params["erase"] = true;
        if (tool == "blur" || tool == "dodge") {
          params[tool == "blur" ? "strength" : "exposure"] = owner_.brush_opacity_->value();
          params["opacity"] = 100;
        }
        if (tool == "clone") {
          if (clone_source_.is_null()) {
            owner_.error_ = "Alt-click to set a clone source";
            owner_.status();
            return 1;
          }
          bool aligned = owner_.aligned_->value();
          if (aligned) {
            if (clone_offset_.is_null())
              clone_offset_ =
                  Json::array({clone_source_[0].get<double>() - first[0].get<double>(),
                               clone_source_[1].get<double>() - first[1].get<double>()});
            params["offset"] = clone_offset_;
          } else {
            params["source"] = clone_source_;
            clone_offset_ = Json();
          }
          params["aligned"] = aligned;
        }
        owner_.execute(command, params);
      }
      redraw();
      return 1;
    }
    return Fl_Widget::handle(event);
  } catch (const std::exception &e) {
    owner_.error_ = e.what();
    owner_.status();
    return 1;
  }
}
void PhotoNavigator::draw() {
  dome_office::GpuClip clip(*this, {x(), y(), w(), h()});
  fl_push_clip(x(), y(), w(), h());
  fl_color(FL_BACKGROUND2_COLOR);
  fl_rectf(x(), y(), w(), h());
  if (!histogram_ && !owner_.canvas_->image().empty()) {
    const auto &image = owner_.canvas_->image();
    double k = std::min(double(w() - 12) / image.width(), double(h() - 12) / image.height());
    int width = int(image.width() * k), height = int(image.height() * k);
    image.draw(*this, {x() + (w() - width) / 2, y() + (h() - height) / 2, width, height},
               {x(), y(), w(), h()});
    auto paper = owner_.canvas_->paper();
    auto doc = owner_.snapshot_.value("document", Json::object());
    double sx = double(width) / std::max(1, doc.value("width", 1)),
           sy = double(height) / std::max(1, doc.value("height", 1)),
           scale = owner_.canvas_->scale();
    int left = x() + (w() - width) / 2, top = y() + (h() - height) / 2;
    fl_color(FL_SELECTION_COLOR);
    int vx = std::max(0, int((owner_.canvas_->x() - paper.x) / scale * sx)),
        vy = std::max(0, int((owner_.canvas_->y() - paper.y) / scale * sy));
    fl_rect(left + vx, top + vy, std::min(width - vx, int(owner_.canvas_->w() / scale * sx)),
            std::min(height - vy, int(owner_.canvas_->h() / scale * sy)));
  } else if (histogram_) {
    fl_font(labelfont(), labelsize());
    fl_color(FL_FOREGROUND_COLOR);
    fl_draw("Luminosity", x() + 8, y() + 24);
    const auto &bins = owner_.canvas_->histogram();
    unsigned maximum = 1;
    for (auto b : bins)
      maximum = std::max(maximum, b);
    for (int i = 0; i < 256; ++i) {
      int height = int(double(bins[i]) / maximum * std::max(1, h() - 34));
      fl_rectf(x() + i * w() / 256, y() + h() - height - 6,
               std::max(1, (i + 1) * w() / 256 - i * w() / 256), height);
    }
  }
  fl_pop_clip();
}
int PhotoNavigator::handle(int event) {
  if (event == FL_PUSH && !histogram_) {
    owner_.zoom_ = 0;
    owner_.canvas_->reset_pan();
    owner_.canvas_->geometry();
    owner_.canvas_->redraw();
    return 1;
  }
  return Fl_Widget::handle(event);
}
} // namespace
int main(int argc, char **argv) {
  try {
    Options options;
    for (int i = 1; i < argc; ++i) {
      std::string argument = argv[i];
      auto value = [&]() -> std::string {
        if (i + 1 >= argc)
          throw std::runtime_error("Missing option value");
        return argv[++i];
      };
      if (argument == "--tray")
        options.tray = true;
      else if (argument == "--engine")
        options.engine = value();
      else if (argument == "--registry")
        options.registry = value();
      else if (argument == "--workspace")
        options.workspace = value();
      else if (argument == "--report")
        options.report = value();
      else if (argument == "--capture")
        options.capture = value();
      else if (argument == "--save-path")
        options.save = value();
      else if (argument == "--quit-after-ms")
        options.lifetime = std::stoi(value());
      else if (argument == "--width")
        options.width = std::clamp(std::stoi(value()), 720, 3840);
      else if (argument == "--height")
        options.height = std::clamp(std::stoi(value()), 500, 2160);
      else if (argument == "--help") {
        std::cout << "PhotoCraft DomeD [image] [--workspace DIR] [--engine "
                     "PATH] [--registry FILE]\n";
        return 0;
      } else if (!argument.starts_with('-') && options.document.empty())
        options.document = std::filesystem::absolute(argument);
      else
        throw std::runtime_error("Unknown option: " + argument);
    }
    if (options.workspace.empty())
      options.workspace = options.document.empty() ? std::filesystem::current_path()
                                                   : options.document.parent_path();
    options.workspace = std::filesystem::canonical(options.workspace);
    std::ifstream input(options.registry);
    Json lock;
    input >> lock;
    Json config;
    for (auto p : lock.at("projects"))
      if (p.at("name") == "photocraft")
        config = p;
    if (config.is_null())
      throw std::runtime_error("PhotoCraft is absent from the registry");
    if (options.engine.empty())
      options.engine =
          std::filesystem::path(DOME_PHOTO_ENGINE_DIR) / config.at("cli").get<std::string>();
    Photo application(config, std::move(options));
    return application.run();
  } catch (const std::exception &e) {
    std::cerr << "PhotoCraft: " << e.what() << '\n';
    return 1;
  }
}
