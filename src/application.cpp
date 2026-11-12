// SPDX-License-Identifier: MIT
#include "desktop-menu.h"
#include "gpu-window.h"
#include "status-item.hpp"
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <artcraft/application.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <gio/gio.h>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace artcraft {
namespace {
class NativeWindow : public dx::GpuWindow {
public:
  using dx::GpuWindow::GpuWindow;
  std::string capture_path;
  Fl_Menu_Bar *shortcut_menu = nullptr; // Owned by this window, never registered globally.
  int handle(int event) override {
    int handled = dx::GpuWindow::handle(event);
    if (handled)
      return handled;
    if (event == FL_SHORTCUT && shortcut_menu && (!Fl::modal() || Fl::modal() == this))
      return shortcut_menu->handle(event);
    return 0;
  }
  void draw() override {
    dx::GpuWindow::draw();
    if (capture_path.empty())
      return;
    const auto path = std::exchange(capture_path, {});
    try {
      const int width = pixel_w(), height = pixel_h();
      if (width < 1 || height < 1 || width > 8192 || height > 8192)
        throw std::runtime_error("QA framebuffer exceeds capture bounds");
      std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
      GLint alignment = 4, buffer = GL_BACK;
      glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
      glGetIntegerv(GL_READ_BUFFER, &buffer);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadBuffer(GL_BACK);
      glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      glReadBuffer(buffer);
      glPixelStorei(GL_PACK_ALIGNMENT, alignment);
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GL error during QA capture");
      const auto temporary = path + ".tmp";
      {
        std::ofstream output(temporary, std::ios::binary);
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y)
          output.write(
              reinterpret_cast<const char *>(pixels.data() + static_cast<size_t>(y) * width * 3),
              width * 3);
        if (!output)
          throw std::runtime_error("Cannot write QA capture");
      }
      std::filesystem::rename(temporary, path);
    } catch (const std::exception &error) {
      fprintf(stderr, "ArtCraft capture: %s\n", error.what());
    }
  }
};
} // namespace
struct Application::Impl {
  ApplicationOptions options;
  Commands commands;
  std::unique_ptr<NativeWindow> window;
  Fl_Menu_Bar *menu = nullptr; // Owned by the window.
  std::unique_ptr<DesktopMenu> exported;
  std::unique_ptr<StatusItem> tray;
  struct Binding {
    Impl *owner;
    std::string id;
    int index;
  };
  std::vector<std::unique_ptr<Binding>> bindings;
  bool started = false, quitting = false, close_command = false;
  int top = 30;

  explicit Impl(ApplicationOptions value) : options(std::move(value)) {
    if (options.id.empty() || options.title.empty() || options.width < 320 || options.height < 240)
      throw std::invalid_argument("ArtCraft needs an app ID, title and a usable window size");
    static bool initialized = false;
    if (!initialized) {
      // DomeD owns both presentation realms. The caller/launcher owns routing.
      // Never discover XR availability or change the Wayland socket here.
      if (!getenv("FLTK_BACKEND"))
        setenv("FLTK_BACKEND", "wayland", 1);
      dx::init();
      setenv("DOME_OFFICE_BDF", dx::selected_font.file.c_str(), 1);
      std::string catalog;
      for (const auto &face : bdf_fonts::discover()) {
        if (face.file.find_first_of("\t\r\n") != std::string::npos ||
            face.family.find_first_of("\t\r\n") != std::string::npos ||
            face.style.find_first_of("\t\r\n") != std::string::npos)
          continue;
        auto line = face.file + "\t" + face.family + "\t" + face.style + "\t" +
                    std::to_string(face.pixels) + "\n";
        if (catalog.size() + line.size() > 120 * 1024)
          break;
        catalog += line;
      }
      setenv("DOME_OFFICE_BDF_CATALOG", catalog.c_str(), 1);
      initialized = true;
    }
    window = std::make_unique<NativeWindow>(options.width, options.height, options.title.c_str());
    window->xclass(options.id.c_str());
    menu = new Fl_Menu_Bar(0, 0, options.width, 30);
    window->shortcut_menu = menu;
    window->end();
    if (dx::dome()) {
      menu->hide();
      top = 0;
    }
    window->size_range(320, 240);
    window->callback(
        [](Fl_Widget *, void *data) {
          auto &self = *static_cast<Impl *>(data);
          // Applications register their own close policy to protect unsaved work.
          if (self.close_command)
            self.invoke("app.close");
          else
            self.close();
        },
        this);
  }
  void close() {
    quitting = true;
    window->hide();
  }
  bool invoke(const std::string &id) {
    try {
      bool executed = commands.invoke(id);
      refresh();
      window->redraw();
      return executed;
    } catch (const std::exception &error) {
      // Do not unwind through an FLTK or D-Bus C callback.
      fl_alert("%s", error.what());
      return false;
    }
  }
  void refresh() {
    for (const auto &binding : bindings) {
      const auto state = commands.state(binding->id);
      auto item = const_cast<Fl_Menu_Item *>(menu->menu() + binding->index);
      if (state.enabled)
        item->activate();
      else
        item->deactivate();
      if (state.checked)
        item->set();
      else
        item->clear();
    }
  }
  static void tick(void *data) {
    auto &self = *static_cast<Impl *>(data);
    try {
      self.refresh();
    } catch (const std::exception &error) {
      fprintf(stderr, "ArtCraft command state: %s\n", error.what());
    }
    Fl::repeat_timeout(.05, tick, data);
  }
  void start() {
    if (started)
      return;
    for (const auto *command : commands.entries()) {
      auto binding = std::make_unique<Binding>(Binding{this, command->id, 0});
      // Store the final index after all insertions: FLTK can relocate/reorder
      // menu arrays while adding subsequent submenu items.
      menu->add(
          command->menu_path.c_str(), command->shortcut,
          [](Fl_Widget *, void *data) {
            auto &binding = *static_cast<Binding *>(data);
            binding.owner->invoke(binding.id);
          },
          binding.get(), command->checkable ? FL_MENU_TOGGLE : 0);
      bindings.push_back(std::move(binding));
    }
    const auto entries = commands.entries();
    for (size_t i = 0; i < bindings.size(); ++i) {
      bindings[i]->index = menu->find_index(entries[i]->menu_path.c_str());
      if (bindings[i]->index < 0)
        throw std::invalid_argument("Invalid FLTK menu path: " + entries[i]->menu_path);
    }
    refresh();
    exported = std::make_unique<DesktopMenu>(*window, *menu);
    if (options.tray)
      tray = std::make_unique<StatusItem>(options.id, options.title, exported->objectPath(),
                                          [this] { window->show(); });
    dx::style(window.get());
    Fl::add_timeout(.05, tick, this);
    started = true;
  }
  ~Impl() {
    Fl::remove_timeout(tick, this);
    tray.reset();
    exported.reset();
  }
};
Application::Application(ApplicationOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
Application::~Application() = default;
void Application::add_command(Command command) {
  if (impl_->started)
    throw std::logic_error("Register ArtCraft commands before show/run");
  impl_->commands.add(std::move(command));
  for (auto entry : impl_->commands.entries())
    if (entry->id == "app.close")
      impl_->close_command = true;
}
bool Application::invoke(const std::string &id) { return impl_->invoke(id); }
Fl_Gl_Window &Application::window() { return *impl_->window; }
int Application::content_top() const { return impl_->top; }
void Application::style() { dx::style(impl_->window.get()); }
void Application::show() {
  impl_->start();
  impl_->window->show();
}
bool Application::hide_to_tray() {
  if (!impl_->tray || !impl_->tray->available())
    return false;
  impl_->window->hide();
  return true;
}
void Application::quit() { impl_->close(); }
int Application::run() {
  show();
  while (!impl_->quitting) {
    Fl::wait(.05);
    for (int n = 0; n < 16 && g_main_context_pending(nullptr); ++n)
      g_main_context_iteration(nullptr, false);
    // FLTK returns immediately when every window is hidden. Keep optional
    // background work/tray activation responsive without spinning the CPU.
    if (!impl_->window->shown())
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}
PlatformInfo Application::platform_info() const {
  PlatformInfo result;
  result.app_id = impl_->options.id;
  result.dome_session = dx::dome();
  result.font_family = dx::font_name;
  result.font_file = dx::selected_font.file;
  result.font_pixels = dx::size;
  result.tray_available = impl_->tray && impl_->tray->available();
  if (impl_->exported) {
    result.menu_service = impl_->exported->serviceName();
    result.menu_path = impl_->exported->objectPath();
  }
  if (impl_->window->shown() && impl_->window->context()) {
    impl_->window->make_current();
    if (auto renderer = glGetString(GL_RENDERER))
      result.renderer = reinterpret_cast<const char *>(renderer);
  }
  return result;
}
BitmapFace document_bitmap_face(const std::string &family, bool bold, bool italic, int pixels) {
  static std::map<std::string, BitmapFace> cache;
  static std::deque<std::string> names;
  pixels = std::clamp(pixels, 8, 256);
  const auto key =
      family + "/" + (bold ? "b" : "") + (italic ? "i" : "") + "/" + std::to_string(pixels);
  if (auto found = cache.find(key); found != cache.end())
    return found->second;
  if (cache.size() >= 256)
    return {dx::font, dx::size, false, false};
  auto faces = bdf_fonts::discover();
  auto chosen = dx::selected_font;
  long score = 100000000;
  auto lower = [](std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
  };
  const auto wanted = lower(family);
  for (const auto &face : faces) {
    auto style = lower(face.style);
    bool b = style.find("bold") != std::string::npos,
         i = style.find("italic") != std::string::npos ||
             style.find("oblique") != std::string::npos;
    long candidate = (lower(face.family) != wanted ? 1000000 : 0) + (b != bold ? 10000 : 0) +
                     (i != italic ? 5000 : 0) + std::abs(face.pixels - pixels);
    if (candidate < score) {
      score = candidate;
      chosen = face;
    }
  }
  // Missing document families retain the same installed face as the native appearance.
  if (score >= 1000000)
    chosen = dx::selected_font;
  while (chosen.pixels * 2 <= 256 && pixels >= chosen.pixels * 1.5)
    chosen = bdf_fonts::scaled(chosen, 2);
  chosen = bdf_fonts::scaled(chosen, 1);
  const int font = FL_FREE_FONT + 1 + int(names.size());
  names.push_back(chosen.id());
  Fl::set_font(font, names.back().c_str());
  auto style = lower(chosen.style);
  BitmapFace face{font, chosen.pixels, style.find("bold") != std::string::npos,
                  style.find("italic") != std::string::npos ||
                      style.find("oblique") != std::string::npos};
  cache[key] = face;
  return face;
}
void Application::request_capture(const std::string &path) {
  if (std::filesystem::path(path).extension() != ".ppm")
    throw std::invalid_argument("QA capture requires a .ppm filename");
  impl_->window->capture_path = path;
  impl_->window->redraw();
}
} // namespace artcraft
