// SPDX-License-Identifier: MIT
#pragma once
#include <artcraft/commands.hpp>
#include <memory>
#include <string>
class Fl_Gl_Window;

namespace artcraft {
struct ApplicationOptions {
  std::string id;
  std::string title;
  int width = 880;
  int height = 600;
  bool tray = false; // Opt-in, never a mandatory editor lifecycle.
};
struct BitmapFace {
  int font = 0, pixels = 16;
  bool bold = false, italic = false;
};
// Resolves only canonical installed BDF faces; never introduces an outline-font fallback.
BitmapFace document_bitmap_face(const std::string &family, bool bold, bool italic, int pixels);
struct PlatformInfo {
  std::string app_id, menu_service, menu_path, font_family, font_file, renderer;
  int font_pixels = 0;
  bool dome_session = false;
  bool tray_available = false;
};

// One window and command registry per instance. Construct after defining the
// application ID; add commands and child widgets before the first show().
// Callbacks and widget ownership stay on the FLTK thread.
class Application {
public:
  explicit Application(ApplicationOptions options);
  ~Application();
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  void add_command(Command command);
  bool invoke(const std::string &id);
  Fl_Gl_Window &window();
  int content_top() const;
  void show();
  void style();        // Apply canonical appearance after adding native controls.
  bool hide_to_tray(); // Returns false if no watcher is available; keeps window.
  void quit();
  int run();
  PlatformInfo platform_info() const;
  // Explicit QA readback after the next complete GL frame, before buffer swap.
  // Captures physical framebuffer pixels to a PPM file, not a CPU widget redraw.
  void request_capture(const std::string &ppm_path);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace artcraft
