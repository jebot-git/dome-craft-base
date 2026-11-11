// SPDX-License-Identifier: MIT
#include <artcraft/application.hpp>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_draw.H>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

namespace {
class Canvas : public Fl_Widget {
public:
  bool selected = false, grid = true;
  int edits = 0;
  std::function<void()> changed;
  Canvas(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void draw() override {
    fl_font(labelfont(), labelsize());
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND2_COLOR); fl_rectf(x(), y(), w(), h());
    if (grid) {
      fl_color(fl_color_average(FL_FOREGROUND_COLOR, FL_BACKGROUND2_COLOR, .12));
      for (int column = x(); column < x() + w(); column += 24)
        fl_line(column, y(), column, y() + h());
      for (int row = y(); row < y() + h(); row += 24)
        fl_line(x(), row, x() + w(), row);
    }
    fl_color(selected ? FL_SELECTION_COLOR : FL_FOREGROUND_COLOR);
    fl_rect(x() + 70, y() + 65, 210, 140);
    fl_line(x() + 70, y() + 205, x() + 280, y() + 65);
    fl_color(FL_FOREGROUND_COLOR);
    // The appearance watcher supplies the native bitmap face and size.
    fl_draw("ArtCraft native canvas", x() + 36, y() + 36);
    fl_draw(selected ? "Selected: use Edit > Apply" : "Click the canvas to select",
            x() + 36, y() + h() - 30);
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event != FL_PUSH) return 0;
    selected = !selected;
    if (changed) changed();
    redraw();
    return 1;
  }
};
struct Probe {
  artcraft::Application &app;
  Canvas &canvas;
  std::string path;
  static void write(void *data) {
    auto &self = *static_cast<Probe *>(data);
    if (!self.path.empty()) {
      const auto info = self.app.platform_info();
      // Line protocol for private integration tests; not a session protocol.
      std::ofstream output(self.path);
      output << info.app_id << '\n' << info.menu_service << '\n' << info.menu_path
             << '\n' << info.font_family << '\n' << info.font_pixels << '\n'
             << info.renderer << '\n' << info.tray_available << '\n'
             << self.canvas.selected << '\n' << self.canvas.edits << '\n'
             << self.canvas.grid << '\n' << self.app.window().shown() << '\n';
    }
    Fl::repeat_timeout(.1, write, data);
  }
};
}
int main(int argc, char **argv) {
  try {
    bool tray = false;
    int lifetime = 0;
    std::string report;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--tray") tray = true;
      else if (arg == "--report" && i + 1 < argc) report = argv[++i];
      else if (arg == "--quit-after-ms" && i + 1 < argc) {
        size_t consumed = 0;
        std::string value = argv[++i];
        lifetime = std::stoi(value, &consumed);
        if (consumed != value.size() || lifetime <= 0 || lifetime > 600000)
          throw std::invalid_argument("Invalid demo lifetime");
      } else if (arg == "--help") {
        std::cout << "artcraft-base-demo [--tray] [--report FILE] [--quit-after-ms N]\n";
        return 0;
      } else throw std::invalid_argument("Unknown or incomplete option: " + arg);
    }
    artcraft::Application app({"org.domed.ArtCraft.BaseDemo", "ArtCraft Base", 880, 600, tray});
    app.window().begin();
    auto canvas = new Canvas(16, app.content_top() + 64, 848,
                             600 - app.content_top() - 80);
    auto select = new Fl_Button(16, app.content_top() + 16, 160, 32, "Select / clear");
    auto apply = new Fl_Button(192, app.content_top() + 16, 140, 32, "Apply");
    app.window().resizable(canvas);
    app.window().end();
    app.add_command({"file.choose", "&File/&Choose file...", FL_CTRL + 'o', false,
      [&] {
        Fl_Native_File_Chooser chooser;
        chooser.title("ArtCraft integration file chooser");
        chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.show(); // No document import is claimed by this demo.
      }, {}});
    app.add_command({"app.close", "&File/&Close", FL_CTRL + 'w', false,
      [&] { app.quit(); }, {}});
    app.add_command({"app.quit", "&File/&Quit", FL_CTRL + 'q', false,
      [&] { app.quit(); }, {}});
    auto update = [&] { if (canvas->selected) apply->activate(); else apply->deactivate(); };
    canvas->changed = update;
    app.add_command({"edit.select", "&Edit/&Select or clear", FL_CTRL + 'a', false,
      [&] { canvas->selected = !canvas->selected; update(); }, {}});
    app.add_command({"edit.apply", "&Edit/&Apply", FL_CTRL + FL_Enter, false,
      [&] { ++canvas->edits; }, [&] { return artcraft::CommandState{canvas->selected, false}; }});
    app.add_command({"view.grid", "&View/&Grid", FL_CTRL + 'g', true,
      [&] { canvas->grid = !canvas->grid; },
      [&] { return artcraft::CommandState{true, canvas->grid}; }});
    if (tray) {
      app.add_command({"window.restore", "&Window/&Restore", 0, false,
        [&] { app.show(); }, {}});
      app.add_command({"window.hide", "&Window/&Hide to tray", 0, false,
        [&] { app.hide_to_tray(); }, [&] {
          return artcraft::CommandState{app.platform_info().tray_available, false}; }});
    }
    select->callback([](Fl_Widget *, void *data) {
      static_cast<artcraft::Application *>(data)->invoke("edit.select");
    }, &app);
    apply->callback([](Fl_Widget *, void *data) {
      static_cast<artcraft::Application *>(data)->invoke("edit.apply");
    }, &app);
    update();
    Probe probe{app, *canvas, report};
    if (!report.empty()) Fl::add_timeout(.2, Probe::write, &probe);
    if (lifetime) Fl::add_timeout(lifetime / 1000., [](void *data) {
      static_cast<artcraft::Application *>(data)->quit();
    }, &app);
    const int result = app.run();
    Fl::remove_timeout(Probe::write, &probe);
    return result;
  } catch (const std::exception &error) {
    std::cerr << "ArtCraft: " << error.what() << '\n';
    return 1;
  }
}
