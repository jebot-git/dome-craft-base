// SPDX-License-Identifier: MIT
#include "editor.hpp"
#include <artcraft/application.hpp>
#include <artcraft/projects.hpp>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace artcraft {
namespace {
struct Options {
  std::filesystem::path document, report, export_path, capture;
  int lifetime = 0;
  bool tray = false;
};
class Editor;
class Canvas : public Fl_Widget {
  Editor &editor_;
  Json gesture_;
  bool dragging_ = false;
  int grab_x_ = 0, grab_y_ = 0;
  int last_paint_x_ = -1, last_paint_y_ = -1;
  GLuint texture_ = 0;
  uint64_t uploaded_ = 0;
  int image_width_ = 0, image_height_ = 0;
  void paint(int x, int y);
public:
  Canvas(int x, int y, int w, int h, Editor &editor) : Fl_Widget(x, y, w, h), editor_(editor) {}
  ~Canvas() override;
  std::array<int, 4> viewport() const;
  void draw() override;
  int handle(int event) override;
};
class TextEditor : public Fl_Text_Editor {
  Application &application_;
public:
  TextEditor(int x, int y, int w, int h, Application &app)
    : Fl_Text_Editor(x, y, w, h), application_(app) {}
  int handle(int event) override {
    if (event == FL_KEYDOWN && Fl::event_state(FL_CTRL)) {
      if (Fl::event_key() == 'z') {
        application_.invoke(Fl::event_state(FL_SHIFT) ? "edit.redo" : "edit.undo");
        return 1;
      }
      if (Fl::event_key() == 'y') { application_.invoke("edit.redo"); return 1; }
    }
    return Fl_Text_Editor::handle(event);
  }
};
class Editor {
  struct ButtonAction { Editor *editor; std::string id; };
  std::vector<std::unique_ptr<ButtonAction>> actions_;
  std::unique_ptr<Fl_Text_Buffer> buffer_;
  TextEditor *text_editor_ = nullptr;
  Fl_Box *status_ = nullptr;
  Fl_Input *cell_ = nullptr, *title_ = nullptr;
  Fl_Multiline_Input *body_ = nullptr;
  Fl_Hold_Browser *slides_ = nullptr;
  bool syncing_ = false;
  std::string status_text_;
  uint64_t captured_revision_ = 0;
  void button(int x, const char *label, const char *command) {
    auto widget = new Fl_Button(x, app.content_top() + 12, 104, 30, label);
    auto action = std::make_unique<ButtonAction>(ButtonAction{this, command});
    widget->callback([](Fl_Widget *, void *data) {
      auto &action = *static_cast<ButtonAction *>(data);
      action.editor->app.invoke(action.id);
    }, action.get());
    actions_.push_back(std::move(action));
  }
  template<class Function> void guard(Function operation) {
    try { operation(); }
    catch (const std::exception &error) { fl_alert("%s", error.what()); sync(); }
  }
  std::filesystem::path choose(bool save, bool exporting = false) {
    Fl_Native_File_Chooser chooser;
    const auto suffix = exporting ? document.export_extension() : extension(document.kind());
    chooser.title(exporting ? "Export test document" : save ? "Save test project" : "Open test project");
    chooser.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE : Fl_Native_File_Chooser::BROWSE_FILE);
    const auto filter = std::string("ArtCraft test file\t*") + suffix;
    chooser.filter(filter.c_str());
    if (save) {
      chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM | Fl_Native_File_Chooser::USE_FILTER_EXT);
      const auto preset = std::string("Untitled") + suffix;
      chooser.preset_file(preset.c_str());
    }
    if (chooser.show() != 0 || !chooser.filename()) return {};
    return chooser.filename();
  }
  bool save(bool as = false) {
    auto path = document.path();
    if (!as && path.empty()) path = initial_path_;
    if (as || path.empty()) path = choose(true);
    if (path.empty()) return false;
    document.save(path); sync(); return true;
  }
  bool discard_or_save() {
    if (!document.dirty()) return true;
    const int choice = fl_choice("Save changes to this test project?", "Cancel", "Discard", "Save");
    if (choice == 2) return save();
    return choice == 1;
  }
  void commands();
  static void report_tick(void *data) {
    auto &self = *static_cast<Editor *>(data);
    try {
      if (!self.options.capture.empty() && self.captured_revision_ != self.revision) {
        self.app.request_capture(self.options.capture.string());
        self.captured_revision_ = self.revision;
      }
      if (self.options.report.empty()) {
        Fl::repeat_timeout(.1, report_tick, data); return;
      }
      const auto info = self.app.platform_info();
      Json value{{"app_id", info.app_id}, {"menu_service", info.menu_service},
        {"menu_path", info.menu_path}, {"font", info.font_family}, {"font_pixels", info.font_pixels},
        {"renderer", info.renderer}, {"dirty", self.document.dirty()},
        {"undo", self.document.can_undo()}, {"redo", self.document.can_redo()},
        {"content", self.document.content()}, {"path", self.document.path().string()},
        {"shown", bool(self.app.window().shown())}};
      value["window"] = {self.app.window().w(), self.app.window().h()};
      if (self.canvas) value["canvas"] = self.canvas->viewport();
      if (self.document.kind() == Kind::grid)
        value["C1"] = evaluate_cell(self.document.content(), 0, 2).text;
      const auto temporary = self.options.report.string() + ".tmp";
      { std::ofstream output(temporary); output << value.dump(); }
      std::filesystem::rename(temporary, self.options.report);
    } catch (const std::exception &error) { std::cerr << "QA report: " << error.what() << '\n'; }
    Fl::repeat_timeout(.1, report_tick, data);
  }
  static void close_tick(void *data) { static_cast<Editor *>(data)->app.invoke("app.close"); }
public:
  Application app;
  Document document;
  Options options;
  Canvas *canvas = nullptr;
  uint64_t revision = 1;
  int row = 0, column = 0, slide = 0, shape = -1;
  std::array<int, 3> brush{107, 171, 255};
  bool presenting = false;
  Editor(Kind kind, Options value)
    : app({std::string(projects[static_cast<size_t>(kind)].app_id),
           std::string(projects[static_cast<size_t>(kind)].name) + " Test", 1000, 700, value.tray}),
      document(kind), options(std::move(value)) {
    if (!options.document.empty()) {
      if (std::filesystem::exists(options.document)) document.load(options.document);
      else {
        if (options.document.extension() != extension(kind))
          throw std::runtime_error("New projects require " + extension(kind));
        // A new requested path becomes the save destination without touching disk.
        initial_path_ = options.document;
      }
    }
    app.window().begin();
    app.window().size_range(720, 460);
    button(16, "New", "file.new"); button(128, "Open", "file.open");
    button(240, "Save", "file.save"); button(352, "Undo", "edit.undo");
    button(464, "Redo", "edit.redo"); button(576, "Export", "file.export");
    const int top = app.content_top() + 60;
    if (kind == Kind::word) {
      buffer_ = std::make_unique<Fl_Text_Buffer>();
      text_editor_ = new TextEditor(16, top, 968, 560 - app.content_top(), app);
      text_editor_->buffer(buffer_.get());
      text_editor_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
      buffer_->add_modify_callback([](int, int, int, int, const char *, void *data) {
        auto &self = *static_cast<Editor *>(data);
        if (self.syncing_) return;
        self.guard([&] {
          std::unique_ptr<char, decltype(&free)> text(self.buffer_->text(), free);
          self.document.edit([&](Json &content) { content["text"] = text.get(); });
          self.changed(false);
        });
      }, this);
      app.window().resizable(text_editor_);
    } else if (kind == Kind::deck) {
      slides_ = new Fl_Hold_Browser(16, top, 180, 550 - app.content_top());
      slides_->callback([](Fl_Widget *, void *data) {
        auto &self = *static_cast<Editor *>(data);
        self.slide = std::max(0, self.slides_->value() - 1); self.sync();
      }, this);
      title_ = new Fl_Input(260, top, 724, 32, "Title");
      body_ = new Fl_Multiline_Input(260, top + 48, 724, 120, "Text");
      title_->when(FL_WHEN_CHANGED); body_->when(FL_WHEN_CHANGED);
      auto edited = [](Fl_Widget *, void *data) {
        auto &self = *static_cast<Editor *>(data);
        if (self.syncing_) return;
        self.guard([&] {
          self.document.edit([&](Json &content) {
            content["slides"][self.slide]["title"] = self.title_->value();
            content["slides"][self.slide]["body"] = self.body_->value();
          });
          self.changed(false);
        });
      };
      title_->callback(edited, this); body_->callback(edited, this);
      canvas = new Canvas(220, top + 186, 764, 358 - app.content_top(), *this);
      app.window().resizable(canvas);
    } else {
      if (kind == Kind::grid) {
        cell_ = new Fl_Input(100, top, 884, 32, "Cell value");
        cell_->when(FL_WHEN_CHANGED);
        cell_->callback([](Fl_Widget *, void *data) {
          auto &self = *static_cast<Editor *>(data);
          if (!self.syncing_) self.guard([&] {
            const std::string value = self.cell_->value();
            self.document.edit([&](Json &content) { content["cells"][self.row][self.column] = value; });
            self.changed(false);
          });
        }, this);
      }
      canvas = new Canvas(16, top + (kind == Kind::grid ? 48 : 0), 968,
                           550 - app.content_top() - (kind == Kind::grid ? 48 : 0), *this);
      app.window().resizable(canvas);
    }
    status_ = new Fl_Box(16, 650, 968, 34);
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    app.window().end();
    commands(); sync();
  }
  std::filesystem::path initial_path_;
  ~Editor() {
    Fl::remove_timeout(report_tick, this); Fl::remove_timeout(close_tick, this);
    if (text_editor_) text_editor_->buffer(nullptr);
  }
  void changed(bool synchronize = true) { ++revision; if (synchronize) sync(); else { status(); if (canvas) canvas->redraw(); } }
  void status() {
    auto path = document.path().empty() ? initial_path_ : document.path();
    status_text_ = (document.dirty() ? "* " : "") + (path.empty() ? std::string("Untitled") : path.filename().string());
    if (document.kind() == Kind::grid)
      status_text_ += "   " + std::string(1, 'A' + column) + std::to_string(row + 1) + " = " + evaluate_cell(document.content(), row, column).text;
    else if (document.kind() == Kind::deck)
      status_text_ += "   Slide " + std::to_string(slide + 1) + "/" + std::to_string(document.content()["slides"].size()) + (presenting ? "   Presentation" : "");
    else if (document.kind() == Kind::photo) status_text_ += "   " + std::to_string(document.content()["width"].get<int>()) + " x " + std::to_string(document.content()["height"].get<int>()) + " RGB; drag to paint";
    else if (document.kind() == Kind::vector) status_text_ += "   Click or drag a rectangle; Object menu adds shapes";
    status_text_ += "   | Native test format";
    status_->copy_label(status_text_.c_str());
    const auto title = std::string(projects[static_cast<size_t>(document.kind())].name) + " Test - " +
      (document.dirty() ? "* " : "") + (path.empty() ? "Untitled" : path.filename().string());
    app.window().copy_label(title.c_str());
  }
  void sync() {
    syncing_ = true;
    if (buffer_) buffer_->text(document.content()["text"].get_ref<const std::string &>().c_str());
    if (cell_) cell_->value(document.content()["cells"][row][column].get_ref<const std::string &>().c_str());
    if (slides_) {
      slide = std::clamp(slide, 0, static_cast<int>(document.content()["slides"].size()) - 1);
      slides_->clear();
      for (const auto &item : document.content()["slides"]) {
        auto name = item["title"].get<std::string>();
        // FLTK browsers interpret @ prefixes as formatting commands.
        if (!name.empty() && name[0] == '@') name = '@' + name;
        slides_->add(name.c_str());
      }
      slides_->value(slide + 1);
      title_->value(document.content()["slides"][slide]["title"].get_ref<const std::string &>().c_str());
      body_->value(document.content()["slides"][slide]["body"].get_ref<const std::string &>().c_str());
    }
    if (document.kind() == Kind::vector && shape >= static_cast<int>(document.content()["shapes"].size())) shape = -1;
    syncing_ = false;
    status();
    if (canvas) canvas->redraw();
  }
  int run() {
    if (!options.report.empty() || !options.capture.empty()) Fl::add_timeout(.2, report_tick, this);
    if (options.lifetime) Fl::add_timeout(options.lifetime / 1000., close_tick, this);
    return app.run();
  }
};
void Editor::commands() {
  auto add = [&](std::string id, std::string path, int key, std::function<void()> execute,
                 std::function<CommandState()> state = {}, bool checked = false) {
    app.add_command({std::move(id), std::move(path), key, checked, std::move(execute), std::move(state)});
  };
  add("file.new", "&File/&New", FL_CTRL + 'n', [&] {
    if (!discard_or_save()) return;
    document.reset(); initial_path_.clear(); shape = -1; slide = 0; changed();
  });
  add("file.open", "&File/&Open...", FL_CTRL + 'o', [&] {
    if (!discard_or_save()) return;
    auto path = choose(false); if (path.empty()) return;
    document.load(path); initial_path_.clear(); shape = -1; slide = 0; changed();
  });
  add("file.save", "&File/&Save", FL_CTRL + 's', [&] {
    save();
  });
  add("file.save-as", "&File/Save &As...", FL_CTRL + FL_SHIFT + 's', [&] { save(true); });
  add("file.export", "&File/&Export...", FL_CTRL + 'e', [&] {
    auto path = options.export_path.empty() ? choose(true, true) : options.export_path;
    if (!path.empty()) document.export_to(path);
  });
  const auto close = [&] { if (discard_or_save()) app.quit(); };
  add("app.close", "&File/&Close", FL_CTRL + 'w', close);
  add("app.quit", "&File/&Quit", FL_CTRL + 'q', close);
  add("edit.undo", "&Edit/&Undo", FL_CTRL + 'z', [&] { document.undo(); changed(); },
      [&] { return CommandState{document.can_undo(), false}; });
  add("edit.redo", "&Edit/&Redo", FL_CTRL + FL_SHIFT + 'z', [&] { document.redo(); changed(); },
      [&] { return CommandState{document.can_redo(), false}; });
  switch (document.kind()) {
    case Kind::word:
      add("word.insert", "&Text/Insert &paragraph", FL_CTRL + FL_Enter, [&] {
        document.edit([](Json &content) { content["text"] = content["text"].get<std::string>() + "A native WordCraft paragraph.\n"; });
        changed();
      }); break;
    case Kind::grid:
      add("grid.example", "&Sheet/Insert &example", 0, [&] {
        document.edit([](Json &content) { content["cells"][0][0] = "2"; content["cells"][0][1] = "3"; content["cells"][0][2] = "=A1+B1*2"; });
        row = 0; column = 2; changed();
      });
      add("grid.clear", "&Sheet/&Clear cell", 0, [&] {
        document.edit([&](Json &content) { content["cells"][row][column] = ""; }); changed();
      }); break;
    case Kind::deck:
      add("deck.add", "&Slide/&Add slide", FL_CTRL + 'm', [&] {
        document.edit([](Json &content) { content["slides"].push_back({{"title", "New slide"}, {"body", "Edit this text in DeckCraft."}}); });
        slide = document.content()["slides"].size() - 1; changed();
      }, [&] { return CommandState{document.content()["slides"].size() < 128, false}; });
      add("deck.delete", "&Slide/&Delete slide", 0, [&] {
        document.edit([&](Json &content) { content["slides"].erase(slide); }); changed();
      }, [&] { return CommandState{document.content()["slides"].size() > 1, false}; });
      add("deck.previous", "&Slide/&Previous", FL_Page_Up, [&] { --slide; sync(); },
        [&] { return CommandState{slide > 0, false}; });
      add("deck.next", "&Slide/&Next", FL_Page_Down, [&] { ++slide; sync(); },
        [&] { return CommandState{slide + 1 < static_cast<int>(document.content()["slides"].size()), false}; });
      add("deck.present", "&View/&Presentation preview", FL_F + 5, [&] {
        presenting = !presenting;
        if (presenting) { title_->hide(); body_->hide(); } else { title_->show(); body_->show(); }
        sync();
      }, [&] { return CommandState{true, presenting}; }, true); break;
    case Kind::photo:
      add("photo.invert", "&Image/&Invert colors", FL_CTRL + 'i', [&] {
        document.edit([](Json &content) { for (auto &value : content["pixels"]) value = 255 - value.get<int>(); }); changed();
      });
      add("photo.clear", "&Image/&Clear canvas", 0, [&] {
        document.edit([](Json &content) { for (auto &value : content["pixels"]) value = 255; }); changed();
      });
      add("photo.blue", "&Brush/&Blue", 0, [&] { brush = {107, 171, 255}; });
      add("photo.red", "&Brush/&Red", 0, [&] { brush = {235, 80, 75}; });
      add("photo.black", "&Brush/&Black", 0, [&] { brush = {20, 20, 20}; }); break;
    case Kind::vector:
      add("vector.add", "&Object/Add &rectangle", FL_CTRL + 'r', [&] {
        document.edit([](Json &content) {
          const int offset = (content["shapes"].size() % 8) * 24;
          content["shapes"].push_back({{"x", 60 + offset}, {"y", 50 + offset},
            {"width", 180}, {"height", 100}, {"color", {107, 171, 255}}});
        }); shape = document.content()["shapes"].size() - 1; changed();
      }, [&] { return CommandState{document.content()["shapes"].size() < 256, false}; });
      add("vector.delete", "&Object/&Delete rectangle", FL_Delete, [&] {
        document.edit([&](Json &content) { content["shapes"].erase(shape); }); shape = -1; changed();
      }, [&] { return CommandState{shape >= 0, false}; });
      add("vector.red", "&Object/Fill &red", 0, [&] {
        document.edit([&](Json &content) { content["shapes"][shape]["color"] = {235, 80, 75}; }); changed();
      }, [&] { return CommandState{shape >= 0, false}; }); break;
  }
  if (options.tray) {
    add("window.restore", "&Window/&Restore", 0, [&] { app.show(); });
    add("window.hide", "&Window/&Hide to tray", 0, [&] { app.hide_to_tray(); },
      [&] { return CommandState{app.platform_info().tray_available, false}; });
  }
}
Canvas::~Canvas() {
  // Destroyed contexts release their textures; delete explicitly while live.
  if (texture_ && editor_.app.window().shown() && editor_.app.window().context()) {
    editor_.app.window().make_current(); glDeleteTextures(1, &texture_);
  }
}
std::array<int, 4> Canvas::viewport() const {
  const auto kind = editor_.document.kind();
  if (kind != Kind::photo && kind != Kind::vector) return {x(), y(), w(), h()};
  const auto &content = editor_.document.content();
  const int logical_width = kind == Kind::photo ? content["width"].get<int>() : 640;
  const int logical_height = kind == Kind::photo ? content["height"].get<int>() : 400;
  const double scale = std::min(double(w()) / logical_width, double(h()) / logical_height);
  const int width = std::max(1, int(logical_width * scale)), height = std::max(1, int(logical_height * scale));
  return {x() + (w() - width) / 2, y() + (h() - height) / 2, width, height};
}
void Canvas::draw() {
  fl_push_clip(x(), y(), w(), h());
  fl_font(labelfont(), labelsize());
  fl_color(FL_BACKGROUND2_COLOR); fl_rectf(x(), y(), w(), h());
  const auto &content = dragging_ ? gesture_ : editor_.document.content();
  switch (editor_.document.kind()) {
    case Kind::grid: {
      const int cell_width = std::max(1, w() / 9), cell_height = std::max(1, h() / 17);
      for (int row = -1; row < 16; ++row)
        for (int column = -1; column < 8; ++column) {
          int left = x() + (column + 1) * cell_width, top = y() + (row + 1) * cell_height;
          if (row == editor_.row && column == editor_.column) {
            fl_color(FL_SELECTION_COLOR); fl_rectf(left, top, cell_width, cell_height);
          }
          fl_color(FL_FOREGROUND_COLOR); fl_rect(left, top, cell_width, cell_height);
          const auto label = row < 0 ? (column < 0 ? "" : std::string(1, 'A' + column)) :
            column < 0 ? std::to_string(row + 1) : evaluate_cell(content, row, column).text;
          fl_draw(label.c_str(), left + 4, top, cell_width - 8, cell_height, FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        }
      break;
    }
    case Kind::deck: {
      const auto &slide = content["slides"][editor_.slide];
      const int title_height = std::clamp(h() / 3, 24, 64);
      fl_color(FL_FOREGROUND_COLOR); fl_rect(x() + 12, y() + 12, w() - 24, h() - 24);
      fl_draw(slide["title"].get_ref<const std::string &>().c_str(), x() + 24, y() + 20, w() - 48, title_height,
              FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
      fl_draw(slide["body"].get_ref<const std::string &>().c_str(), x() + 24, y() + 20 + title_height, w() - 48, std::max(1, h() - 40 - title_height),
              FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
      break;
    }
    case Kind::photo: {
      auto &owner = editor_.app.window();
      const int width = content["width"], height = content["height"];
      if (!owner.context_valid()) { texture_ = 0; uploaded_ = 0; image_width_ = 0; }
      glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);
      glDisable(GL_BLEND); glEnable(GL_TEXTURE_2D);
      if (!texture_) glGenTextures(1, &texture_);
      glBindTexture(GL_TEXTURE_2D, texture_);
      if (uploaded_ != editor_.revision || image_width_ != width || image_height_ != height) {
        std::vector<unsigned char> pixels; pixels.reserve(width * height * 3);
        for (const auto &value : content["pixels"]) pixels.push_back(value.get<unsigned char>());
        GLint alignment = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (image_width_ != width || image_height_ != height)
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        image_width_ = width; image_height_ = height; uploaded_ = editor_.revision;
      }
      GLint matrix = GL_MODELVIEW; glGetIntegerv(GL_MATRIX_MODE, &matrix);
      glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
      glOrtho(0, owner.w(), owner.h(), 0, -1, 1);
      glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glColor3f(1, 1, 1);
      const auto [left, top, draw_width, draw_height] = viewport();
      glBegin(GL_QUADS);
      glTexCoord2f(0, 0); glVertex2i(left, top); glTexCoord2f(1, 0); glVertex2i(left + draw_width, top);
      glTexCoord2f(1, 1); glVertex2i(left + draw_width, top + draw_height); glTexCoord2f(0, 1); glVertex2i(left, top + draw_height);
      glEnd(); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(matrix); glPopAttrib();
      break;
    }
    case Kind::vector:
      for (size_t i = 0; i < content["shapes"].size(); ++i) {
        const auto &shape = content["shapes"][i]; const auto &color = shape["color"];
        const auto [vx, vy, vw, vh] = viewport();
        const int left = vx + shape["x"].get<int>() * vw / 640, top = vy + shape["y"].get<int>() * vh / 400;
        const int width = std::max(1, shape["width"].get<int>() * vw / 640), height = std::max(1, shape["height"].get<int>() * vh / 400);
        fl_color(fl_rgb_color(color[0].get<unsigned char>(), color[1].get<unsigned char>(), color[2].get<unsigned char>()));
        fl_rectf(left, top, width, height);
        if (static_cast<int>(i) == editor_.shape) { fl_color(FL_FOREGROUND_COLOR); fl_rect(left - 2, top - 2, width + 4, height + 4); }
      }
      break;
    case Kind::word: break;
  }
  fl_pop_clip();
}
void Canvas::paint(int event_x, int event_y) {
  const int width = gesture_["width"], height = gesture_["height"];
  const auto [left, top, draw_width, draw_height] = viewport();
  const int px = std::clamp((event_x - left) * width / draw_width, 0, width - 1);
  const int py = std::clamp((event_y - top) * height / draw_height, 0, height - 1);
  int from_x = last_paint_x_ < 0 ? px : last_paint_x_, from_y = last_paint_y_ < 0 ? py : last_paint_y_;
  const int dx = std::abs(px - from_x), dy = -std::abs(py - from_y);
  const int sx = from_x < px ? 1 : -1, sy = from_y < py ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    for (int channel = 0; channel < 3; ++channel)
      gesture_["pixels"][(from_y * width + from_x) * 3 + channel] = editor_.brush[channel];
    if (from_x == px && from_y == py) break;
    const int twice = 2 * error;
    if (twice >= dy) { error += dy; from_x += sx; }
    if (twice <= dx) { error += dx; from_y += sy; }
  }
  last_paint_x_ = px; last_paint_y_ = py;
  ++editor_.revision; redraw();
}
int Canvas::handle(int event) {
  const auto kind = editor_.document.kind();
  if (event == FL_HIDE && dragging_) { dragging_ = false; ++editor_.revision; }
  if (event == FL_KEYDOWN && kind == Kind::deck && Fl::event_key() == FL_Escape && editor_.presenting) {
    editor_.app.invoke("deck.present"); return 1;
  }
  if (event == FL_PUSH && Fl::event_button() == FL_LEFT_MOUSE) {
    const auto [left, top, draw_width, draw_height] = viewport();
    if ((kind == Kind::photo || kind == Kind::vector) &&
        (Fl::event_x() < left || Fl::event_y() < top || Fl::event_x() >= left + draw_width || Fl::event_y() >= top + draw_height)) return 0;
    if (kind == Kind::grid) {
      editor_.column = std::clamp((Fl::event_x() - x()) / std::max(1, w() / 9) - 1, 0, 7);
      editor_.row = std::clamp((Fl::event_y() - y()) / std::max(1, h() / 17) - 1, 0, 15);
      editor_.sync(); return 1;
    }
    if (kind == Kind::photo) { gesture_ = editor_.document.content(); dragging_ = true; last_paint_x_ = last_paint_y_ = -1; paint(Fl::event_x(), Fl::event_y()); return 1; }
    if (kind == Kind::vector) {
      const int px = (Fl::event_x() - left) * 640 / draw_width, py = (Fl::event_y() - top) * 400 / draw_height;
      editor_.shape = -1;
      const auto &shapes = editor_.document.content()["shapes"];
      for (int i = static_cast<int>(shapes.size()) - 1; i >= 0; --i) {
        const auto &shape = shapes[i];
        if (px >= shape["x"].get<int>() && py >= shape["y"].get<int>() &&
            px < shape["x"].get<int>() + shape["width"].get<int>() && py < shape["y"].get<int>() + shape["height"].get<int>()) {
          editor_.shape = i; grab_x_ = px - shape["x"].get<int>(); grab_y_ = py - shape["y"].get<int>();
          gesture_ = editor_.document.content(); dragging_ = true; break;
        }
      }
      editor_.sync(); return 1;
    }
  }
  if (event == FL_DRAG && dragging_) {
    if (kind == Kind::photo) paint(Fl::event_x(), Fl::event_y());
    else if (kind == Kind::vector && editor_.shape >= 0) {
      const auto [left, top, draw_width, draw_height] = viewport();
      auto &shape = gesture_["shapes"][editor_.shape];
      shape["x"] = std::clamp((Fl::event_x() - left) * 640 / draw_width - grab_x_, 0, 640 - shape["width"].get<int>());
      shape["y"] = std::clamp((Fl::event_y() - top) * 400 / draw_height - grab_y_, 0, 400 - shape["height"].get<int>());
      redraw();
    }
    return 1;
  }
  if (event == FL_RELEASE && dragging_) {
    dragging_ = false;
    try { editor_.document.replace(std::move(gesture_)); editor_.changed(); }
    catch (const std::exception &error) { fl_alert("%s", error.what()); }
    return 1;
  }
  return 0;
}
}
int run_editor(Kind kind, int argc, char **argv) {
  try {
    Options options;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--help") {
        std::cout << kind_name(kind) << " [project" << extension(kind) << "] [--tray] [--report FILE] [--capture FILE.ppm] [--export FILE] [--quit-after-ms N]\n"
          << "Experimental native test editor; no Office/ODF import.\n";
        return 0;
      }
      if (argument == "--tray") options.tray = true;
      else if (argument == "--report" && i + 1 < argc) options.report = argv[++i];
      else if (argument == "--capture" && i + 1 < argc) options.capture = argv[++i];
      else if (argument == "--export" && i + 1 < argc) options.export_path = argv[++i];
      else if (argument == "--quit-after-ms" && i + 1 < argc) {
        const std::string value = argv[++i]; size_t consumed = 0;
        options.lifetime = std::stoi(value, &consumed);
        if (consumed != value.size() || options.lifetime < 1 || options.lifetime > 600000)
          throw std::invalid_argument("Invalid test lifetime");
      } else if (!argument.empty() && argument[0] != '-' && options.document.empty()) options.document = argument;
      else throw std::invalid_argument("Unknown or incomplete argument: " + argument);
    }
    Editor editor(kind, std::move(options));
    return editor.run();
  } catch (const std::exception &error) {
    std::cerr << kind_name(kind) << ": " << error.what() << '\n';
    return 1;
  }
}
} // namespace artcraft
