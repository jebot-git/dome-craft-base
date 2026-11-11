// SPDX-License-Identifier: MIT
#include "office-view.hpp"
#include "printing.hpp"
#include "command-form.hpp"
#include <artcraft/application.hpp>
#include <FL/Fl_Tabs.H>
#include <artcraft/upstream_session.hpp>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <deque>
#include <set>
#include <unordered_map>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
struct Options {
  std::filesystem::path registry;
  std::filesystem::path engine, workspace, document, report, capture, save_path;
  int lifetime = 0;
  bool tray = false;
};
class Port {
  // The text buffer must outlive widgets even when construction throws.
  std::unique_ptr<Fl_Text_Buffer> buffer_ = std::make_unique<Fl_Text_Buffer>();
  artcraft::Application app_;
  Json config_, snapshot_ = Json::object(), last_result_;
  Options options_;
  std::unique_ptr<artcraft::UpstreamSession> engine_;
  std::future<Json> job_;
  bool busy_ = false, fault_ = false, dirty_ = false;
  int page_ = 0;
  size_t menu_commands_ = 0;
  bool preview_mode_ = false, playing_ = false, modal_ = false;
  std::deque<std::vector<std::pair<std::string, Json>>> queued_;
  std::string operation_, error_, selected_;
  std::filesystem::path pending_path_;
  std::vector<Json> visible_;
  std::unordered_map<std::string, bool> enabled_;
  dome_office::View *office_ = nullptr;
  Fl_Group *panes_ = nullptr;
  Fl_Tabs *tabs_ = nullptr;
  bool diagnostics_visible_ = false, paragraph_editor_visible_ = false;
  unsigned long raster_epoch_ = 0;
  std::map<int, Json> thumbnails_;
  Fl_Group *diagnostics_ = nullptr;
  Fl_Text_Display *document_ = nullptr;
  Fl_Hold_Browser *commands_ = nullptr;
  Fl_Input *filter_ = nullptr;
  Fl_Multiline_Input *params_ = nullptr;
  Fl_Box *status_ = nullptr;
  Fl_Button *run_ = nullptr;
  bool enabled(const std::string &id) const {
    if (busy_ || fault_)
      return false;
    auto found = enabled_.find(id);
    return found != enabled_.end() && found->second;
  }
  void status() {
    std::string value =
        busy_            ? "Upstream engine: " + operation_ + "..."
        : fault_         ? "Engine unavailable: " + error_
        : error_.empty() ? (dirty_ ? "Unsaved upstream changes | " : "Upstream session | ") +
                               std::to_string(snapshot_.value("commands", Json::array()).size()) +
                               " commands | " + options_.workspace.string()
                         : error_;
    if (office_ && office_->pending() && !busy_)
      value = "Apply the current text edit before saving or changing selection. " + value;
    if (!diagnostics_visible_) {
      value = error_.empty() ? (busy_ ? "Working..." : "Ready") : error_;
      office_->message(value);
    }
    status_->copy_label(value.c_str());
    if (enabled(selected_))
      run_->activate();
    else
      run_->deactivate();
  }
  void filter() {
    std::string needle = filter_->value();
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    commands_->clear();
    visible_.clear();
    for (const auto &command : snapshot_.value("commands", Json::array())) {
      auto text = command.value("id", "") + " | " + command.value("label", "");
      auto lowered = text;
      std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (!needle.empty() && lowered.find(needle) == std::string::npos)
        continue;
      auto label = std::string(!command.value("enabled_known", true) ? "? "
                               : command.value("enabled", false)     ? "  "
                                                                     : "- ") +
                   text;
      commands_->add(label.c_str());
      visible_.push_back(command);
      if (command.value("id", "") == selected_)
        commands_->value(visible_.size());
    }
  }
  void show_document(bool refilter = true) {
    const auto document = snapshot_.value("document", Json::object());
    auto title = document.value("title", std::string());
    if (title.empty() && document.value("path", Json()).is_string())
      title = std::filesystem::path(document.at("path").get<std::string>()).filename().string();
    if (title.empty())
      title = options_.document.empty() ? "Untitled" : options_.document.filename().string();
    title += dirty_ ? " * - " : " - ";
    title += config_.at("title").get<std::string>();
    app_.window().copy_label(title.c_str());
    std::string value = "Selected command: " + selected_ + "\n";
    for (const auto &command : snapshot_.value("commands", Json::array())) {
      if (command.value("id", "") != selected_)
        continue;
      auto description = command.value("params", Json());
      value += "Parameters: " +
               (description.is_string() ? description.get<std::string>() : description.dump()) +
               "\n\n";
      if (!command.value("enabled_known", true))
        value += "Availability is checked by the upstream engine when run.\n\n";
      break;
    }
    value += "Engine diagnostics (native document view is in the Document tab)\n\n" +
             snapshot_.value("document", Json::object()).dump(2);
    if (value.size() > 512 * 1024) {
      size_t end = 512 * 1024;
      while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
        --end;
      value.resize(end);
      value += "\n[Inspection display truncated; the upstream document is unchanged.]\n";
    }
    buffer_->text(value.c_str());
    enabled_.clear();
    for (const auto &command : snapshot_.value("commands", Json::array()))
      enabled_[command.value("id", std::string())] = command.value("enabled", false);
    office_->update(snapshot_, busy_);
    if (refilter)
      filter();
    status();
    app_.window().redraw();
    if (!options_.capture.empty())
      app_.request_capture(options_.capture.string());
  }
  void start(std::string operation, std::function<Json()> task) {
    if (busy_ || fault_)
      return;
    if (operation != "page" && operation != "page-preview")
      ++raster_epoch_;
    operation_ = std::move(operation);
    error_.clear();
    busy_ = true;
    status();
    office_->update(snapshot_, true);
    job_ = std::async(std::launch::async, [this, task = std::move(task)] {
      auto result = task();
      auto snapshot = operation_ == "show" ? snapshot_ : engine_->snapshot();
      if (operation_ == "show") {
        snapshot["view"]["raster"] = result;
        return Json{{"result", std::move(result)}, {"snapshot", std::move(snapshot)}};
      }
      int count = snapshot.at("document").value("pages", 1);
      if (config_.at("name") == "wordcraft")
        page_ = std::clamp(page_, 0, std::max(0, count - 1));
      if (config_.at("name") == "wordcraft" && operation_ != "page" &&
          operation_ != "page-preview") {
        auto caret = snapshot["view"].value("layout", Json::object()).value("caret", Json());
        if (caret.is_object())
          page_ = caret.value("page", page_);
      }
      if (config_.at("name") != "gridcraft" || preview_mode_) {
        snapshot["view"]["raster"] = engine_->render(page_);
        page_ = snapshot["view"]["raster"].value("page", page_);
      }
      if (config_.at("name") == "wordcraft") {
        snapshot["view"]["reveal_page"] = operation_ == "page";
        snapshot["view"]["raster_epoch"] = std::to_string(raster_epoch_);
        snapshot["view"]["rasters"] = Json::array({snapshot["view"]["raster"]});
        for (int adjacent : {page_ - 1, page_ + 1})
          if (adjacent >= 0 && adjacent < count)
            snapshot["view"]["rasters"].push_back(engine_->render(adjacent));
      }
      if (config_.at("name") == "deckcraft") {
        const int selected =
                      snapshot.at("document").value("selection", Json::object()).value("slide", 0),
                  slides = snapshot.at("document").value("slides", Json::array()).size();
        if (operation_ == "startup" || operation_ == "new" || operation_ == "open")
          thumbnails_.clear();
        for (int index = std::max(0, selected - 5); index < std::min(slides, selected + 6); ++index)
          if (index == selected || !thumbnails_.contains(index)) {
            auto image = engine_->execute("file.render",
                                          {{"slide", index}, {"scale", .18}, {"edit", false}});
            image["slide"] = index;
            thumbnails_[index] = std::move(image);
          }
        while (thumbnails_.size() > 32) {
          auto far = thumbnails_.begin();
          for (auto i = thumbnails_.begin(); i != thumbnails_.end(); ++i)
            if (std::abs(i->first - selected) > std::abs(far->first - selected))
              far = i;
          thumbnails_.erase(far);
        }
        snapshot["view"]["thumbnails"] = Json::array();
        for (const auto &[index, image] : thumbnails_)
          if (index < slides)
            snapshot["view"]["thumbnails"].push_back(image);
      }
      snapshot["view"]["preview_mode"] = preview_mode_;
      return Json{{"result", std::move(result)}, {"snapshot", std::move(snapshot)}};
    });
  }
  void dispatch_queue() {
    if (busy_ || queued_.empty() || fault_)
      return;
    if (queued_.front().size() == 1 && queued_.front()[0].first == "@domed.pagePreview") {
      page_ = queued_.front()[0].second.at("page");
      queued_.pop_front();
      start("page-preview", [] { return Json::object(); });
      return;
    }
    auto commands = std::move(queued_.front());
    queued_.pop_front();
    while (!queued_.empty() && commands.size() < 64) {
      auto next = std::move(queued_.front());
      queued_.pop_front();
      commands.insert(commands.end(), std::make_move_iterator(next.begin()),
                      std::make_move_iterator(next.end()));
    }
    if (commands.size() == 1 && commands[0].first == "@domed.show") {
      const auto action = commands[0].second.at("action").get<std::string>();
      if (action == "exit") {
        playing_ = false;
        app_.window().fullscreen_off();
        office_->presentation(false);
      }
      start(action == "exit" ? "refresh" : "show",
            [this, action] { return engine_->show(action); });
      return;
    }
    for (const auto &[id, params] : commands)
      if (!id.starts_with("caret.") && !id.starts_with("selection.") && !id.starts_with("slide.go"))
        dirty_ = true;
    start("office-edit", [this, commands = std::move(commands)] {
      Json result;
      for (const auto &[id, params] : commands) {
        if (id == "@domed.viewport") {
          engine_->viewport(params.at("row"), params.at("column"));
          result = Json::object();
        } else if (id == "@domed.pointer")
          result = engine_->pointer(params);
        else if (id == "@domed.wordSelection")
          result = engine_->select_text(params);
        else
          result = engine_->execute(id, params);
      }
      return result;
    });
  }
  bool discard() {
    bool allowed =
        (!dirty_ && !office_->pending()) ||
        fl_choice("Discard unsaved upstream changes?", "Cancel", "Discard", nullptr) == 1;
    if (allowed)
      office_->discard_pending();
    return allowed;
  }
  std::filesystem::path choose(bool save, const std::string &extension = "") {
    Fl_Native_File_Chooser chooser;
    chooser.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                      : Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.title(save ? "Save upstream document" : "Open upstream document");
    chooser.directory(options_.workspace.c_str());
    if (save) {
      const auto name =
          std::string("Untitled") +
          (extension.empty() ? config_.value("default_extension", ".json") : extension);
      chooser.preset_file(name.c_str());
      chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    }
    if (chooser.show() || !chooser.filename())
      return {};
    return std::filesystem::absolute(chooser.filename());
  }
  void choose_font() {
    const auto families =
        snapshot_.value("bitmap", Json::object()).value("families", Json::array());
    if (families.empty())
      throw std::runtime_error("No installed bitmap font families");
    modal_ = true;
    struct Modal {
      bool &value;
      ~Modal() { value = false; }
    } modal{modal_};
    artcraft::Application dialog(
        {"org.domed." + config_.at("title").get<std::string>() + ".FontDialog", "Font", 520, 420,
         false});
    dialog.window().set_modal();
    dialog.window().begin();
    int top = dialog.content_top();
    auto *browser = new Fl_Hold_Browser(12, top + 12, 496, 310 - top);
    browser->format_char(0);
    for (const auto &family : families)
      browser->add(family.get<std::string>().c_str());
    browser->value(1);
    bool apply = false;
    dialog.add_command({"font.apply",
                        "&Font/&Apply",
                        FL_Enter,
                        false,
                        [&] {
                          apply = true;
                          dialog.quit();
                        },
                        {}});
    dialog.add_command(
        {"app.close", "&Font/&Cancel", FL_Escape, false, [&] { dialog.quit(); }, {}});
    auto *button = new Fl_Button(12, 340, 130, 32, "Apply");
    button->callback(
        [](Fl_Widget *, void *data) {
          static_cast<artcraft::Application *>(data)->invoke("font.apply");
        },
        &dialog);
    auto *cancel = new Fl_Button(160, 340, 130, 32, "Cancel");
    cancel->callback(
        [](Fl_Widget *, void *data) { static_cast<artcraft::Application *>(data)->quit(); },
        &dialog);
    dialog.window().resizable(browser);
    dialog.window().end();
    dialog.run();
    if (!apply || browser->value() < 1)
      return;
    const auto family = families.at(browser->value() - 1).get<std::string>();
    const std::string name = config_.at("name");
    const auto command = name == "gridcraft" ? "home.fontName" : "format.font";
    Json params = {{name == "deckcraft" ? "family" : "name", family}};
    dirty_ = true;
    start("font", [this, command, params] { return engine_->execute(command, params); });
  }
  void execute_selected() {
    if (!enabled(selected_))
      return;
    auto params = Json::parse(params_->value(), [](int depth, Json::parse_event_t, Json &) {
      if (depth > 32)
        throw std::runtime_error("Command parameters are too deeply nested");
      return true;
    });
    if (!params.is_object() || params.dump().size() > 128 * 1024)
      throw std::runtime_error("Provide a JSON parameter object below 128 KiB");
    const auto command = selected_;
    dirty_ = true; // Conservative until the upstream dirty-state adapters land.
    start(command, [this, command, params] { return engine_->execute(command, params); });
  }
  void tick() {
    if (busy_ && job_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      try {
        auto reply = job_.get();
        last_result_ = reply.at("result");
        snapshot_ = reply.at("snapshot");
        const auto &document = snapshot_.at("document");
        if (document.contains("path"))
          options_.document = document.at("path").is_string()
                                  ? std::filesystem::path(document.at("path").get<std::string>())
                                  : std::filesystem::path();
        if (document.contains("dirty"))
          dirty_ = document.value("dirty", dirty_);
        if (operation_ == "print") {
          auto count = last_result_.size();
          dome_office::print_pages(last_result_);
          last_result_ = {{"pages", count}};
        }
        if (operation_ == "copy" && last_result_.contains("text")) {
          auto text = last_result_.at("text").get<std::string>();
          Fl::copy(text.c_str(), int(text.size()), 1);
        }
        if (operation_ == "office-edit")
          office_->discard_pending();
        if (operation_ == "save" || operation_ == "open")
          options_.document = pending_path_;
        if (operation_ == "new")
          options_.document.clear();
        if (operation_ == "save" || operation_ == "open" || operation_ == "new" ||
            operation_ == "startup")
          dirty_ = false;
      } catch (const std::exception &error) {
        error_ = error.what();
        fault_ = !engine_ || !engine_->available();
      }
      busy_ = false;
      show_document();
      if (!queued_.empty() && error_.empty())
        dispatch_queue();
    }
    if (playing_ && !busy_ && queued_.empty() && error_.empty() &&
        !last_result_.value("ended", false) && !last_result_.value("exit", false))
      start("show", [this] { return engine_->show("frame"); });
    status();
    if (!options_.report.empty()) {
      auto info = app_.platform_info();
      Json report{
          {"application", config_.at("name")},
          {"revision", config_.at("revision")},
          {"app_id", info.app_id},
          {"native_menu_commands", menu_commands_},
          {"font", info.font_family},
          {"font_file", info.font_file},
          {"font_policy", "BDF-only native views and attested upstream layout/render/export"},
          {"ui", office_->ui_state()},
          {"pending_edit", office_->pending()},
          {"renderer", info.renderer},
          {"menu_service", info.menu_service},
          {"menu_path", info.menu_path},
          {"busy", busy_},
          {"playing", playing_},
          {"queued", queued_.size()},
          {"dirty", dirty_},
          {"error", error_},
          {"snapshot", snapshot_},
          {"result", last_result_}};
      if (report["snapshot"].contains("view") && report["snapshot"]["view"].contains("raster")) {
        auto &raster = report["snapshot"]["view"]["raster"];
        if (raster.contains("png")) {
          raster["png_bytes"] = raster["png"].get_ref<const std::string &>().size();
          raster.erase("png");
        }
      }
      if (report["snapshot"].contains("view") && report["snapshot"]["view"].contains("rasters"))
        for (auto &raster : report["snapshot"]["view"]["rasters"])
          if (raster.contains("png")) {
            raster["png_bytes"] = raster["png"].get_ref<const std::string &>().size();
            raster.erase("png");
          }
      if (report["snapshot"].contains("view") && report["snapshot"]["view"].contains("thumbnails"))
        for (auto &image : report["snapshot"]["view"]["thumbnails"])
          if (image.contains("png")) {
            image["png_bytes"] = image["png"].get_ref<const std::string &>().size();
            image.erase("png");
          }
      auto temporary = options_.report.string() + ".tmp";
      {
        std::ofstream output(temporary);
        output << report.dump();
      }
      std::filesystem::rename(temporary, options_.report);
    }
  }
  static void tick_callback(void *data) {
    auto &self = *static_cast<Port *>(data);
    try {
      self.tick();
    } catch (const std::exception &error) {
      self.error_ = error.what();
      self.status();
    }
    Fl::repeat_timeout(1. / 30., tick_callback, data);
  }
  static void close_callback(void *data) { static_cast<Port *>(data)->app_.invoke("app.close"); }

public:
  Port(Json config, Options options)
      : app_({"org.domed." + config.at("title").get<std::string>(),
              config.at("title").get<std::string>(), 1100, 760, options.tray}),
        config_(std::move(config)), options_(std::move(options)) {
    app_.window().size_range(720, 500);
    app_.window().begin();
    const bool word = config_.at("name") == "wordcraft";
    const bool mac = true;
    const auto title = config_.at("title").get<std::string>();
    if (mac)
      panes_ = new Fl_Group(0, app_.content_top(), 1100, 760 - app_.content_top());
    else {
      tabs_ = new Fl_Tabs(8, app_.content_top() + 4, 1084, 674 - app_.content_top());
      panes_ = tabs_;
    }
    office_ = new dome_office::View(
        mac ? 0 : 8, app_.content_top() + (mac ? 0 : 34), mac ? 1100 : 1084,
        (mac ? 760 : 644) - app_.content_top(), config_.at("name"),
        [this](std::vector<std::pair<std::string, Json>> commands) {
          if (queued_.size() >= 1024)
            throw std::runtime_error("Typing queue is full; wait for the engine");
          queued_.push_back(std::move(commands));
          if (!busy_)
            dispatch_queue();
        },
        [this](const std::string &id) {
          if (id == "@appearance")
            app_.style();
          else
            app_.invoke(id);
        });
    diagnostics_ =
        new Fl_Group(8, app_.content_top() + 34, 1084, 644 - app_.content_top(), "Commands");
    const int top = app_.content_top() + 30;
    // Resize both columns proportionally when DomeD constrains the window,
    // rather than keeping the command browser at its full desktop width.
    auto content_bounds = new Fl_Box(16, top + 54, 1068, 620 - top);
    filter_ = new Fl_Input(80, top + 12, 350, 30, "Find");
    filter_->when(FL_WHEN_CHANGED);
    filter_->callback([](Fl_Widget *, void *data) { static_cast<Port *>(data)->filter(); }, this);
    commands_ = new Fl_Hold_Browser(16, top + 54, 414, 620 - top);
    commands_->callback(
        [](Fl_Widget *, void *data) {
          auto &self = *static_cast<Port *>(data);
          const int selected = self.commands_->value() - 1;
          if (selected < 0 || static_cast<size_t>(selected) >= self.visible_.size())
            return;
          self.selected_ = self.visible_[selected].value("id", "");
          self.show_document(false);
        },
        this);
    params_ = new Fl_Multiline_Input(448, top + 32, 636, 96, "Parameters (JSON)");
    params_->align(FL_ALIGN_TOP_LEFT);
    params_->value(config_.at("smoke_params").dump().c_str());
    run_ = new Fl_Button(448, top + 140, 190, 30, "Run command");
    run_->callback(
        [](Fl_Widget *, void *data) { static_cast<Port *>(data)->app_.invoke("engine.run"); },
        this);
    document_ = new Fl_Text_Display(448, top + 186, 636, 488 - top);
    document_->buffer(buffer_.get());
    document_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    diagnostics_->resizable(content_bounds);
    diagnostics_->end();
    panes_->end();
    if (tabs_)
      tabs_->value(office_);
    else
      diagnostics_->hide();
    status_ = new Fl_Box(mac ? 8 : 16, mac ? 734 : 690, mac ? 1084 : 1068, mac ? 26 : 50);
    status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    if (mac)
      status_->hide();
    panes_->resizable(office_);
    app_.window().resizable(panes_);
    app_.window().end();
    std::vector<artcraft::Command> native_commands;
    std::set<std::string> registered;
    std::set<int> shortcuts;
    std::set<std::string> menu_paths;
    auto normalized = [](std::string path) {
      path.erase(std::remove(path.begin(), path.end(), '&'), path.end());
      return path;
    };
    auto add = [this, &registered, &shortcuts, &menu_paths, &normalized, &native_commands](
                   const char *id, const char *path, int shortcut, std::function<void()> action,
                   const char *engine_command = nullptr) {
      const std::string dependency = engine_command ? engine_command : "";
      const std::string registry_id = id;
      menu_paths.insert(normalized(path));
      registered.insert(registry_id);
      if (shortcut)
        shortcuts.insert(shortcut);
      native_commands.push_back(
          {id, path, shortcut, false, std::move(action), [this, dependency, registry_id] {
             bool allowed =
                 !busy_ && !modal_ && !fault_ && (dependency.empty() || enabled(dependency));
             if ((registry_id == "edit.apply" || registry_id == "edit.discard") &&
                 !office_->pending())
               allowed = false;
             if (office_->pending() && registry_id != "edit.apply" &&
                 registry_id != "edit.discard" && registry_id != "app.close" &&
                 registry_id != "app.quit" && registry_id != "file.new" &&
                 registry_id != "file.open")
               allowed = false;
             if (registry_id == "app.close" || registry_id == "app.quit")
               allowed = !busy_;
             if (registry_id == "engine.run")
               allowed = allowed && enabled(selected_);
             if (registry_id == "show.stop")
               allowed = playing_;
             if (registry_id == "window.hide")
               allowed = allowed && app_.platform_info().tray_available;
             return artcraft::CommandState{allowed, false};
           }});
    };
    if (mac) {
      add("app.about", ("&" + title + "/&About " + title).c_str(), 0, [title] {
        fl_message("%s for DomeD\nNative FLTK interface with the pinned Storytold "
                   "engine.\nDocument text and controls use installed bitmap fonts.",
                   title.c_str());
      });
      if (word)
        add("tools.wordCount", "&Tools/&Word Count...", 0, [this] {
          fl_message("%d words", snapshot_.value("document", Json::object()).value("words", 0));
        });
      add("window.document", "&Window/&Document", 0, [this] {
        app_.show();
        office_->focus_document();
      });
      add("help.application", ("&Help/&" + title + " Help").c_str(), 0, [] {
        fl_message("Edit directly in the document workspace.\nUse the ribbon for formatting and "
                   "the universal menu for file actions.\nView offers optional text tools and the "
                   "command workbench.");
      });
      add("view.commands", "&View/&Command workbench", 0, [this] {
        diagnostics_visible_ = !diagnostics_visible_;
        if (diagnostics_visible_) {
          office_->hide();
          diagnostics_->resize(panes_->x(), panes_->y(), panes_->w(), panes_->h());
          diagnostics_->show();
        } else {
          diagnostics_->hide();
          office_->show();
          office_->resize(panes_->x(), panes_->y(), panes_->w(), panes_->h());
          office_->focus_document();
        }
        app_.window().redraw();
      });
      if (config_.at("name") != "gridcraft")
        add("view.paragraphEditor", word ? "&View/&Paragraph text tool" : "&View/Shape &text tool",
            0, [this] {
              paragraph_editor_visible_ = !paragraph_editor_visible_;
              office_->paragraph_editor(paragraph_editor_visible_);
            });
      add("format.fontDialog", "&Format/Font &dialog...", 0, [this] { choose_font(); });
      if (word)
        add("edit.find", "&Edit/&Find...", FL_CTRL + 'f', [this] { office_->find(); });
    }
    add("file.new", "&File/&New", FL_CTRL + 'n', [this] {
      if (discard())
        start("new", [this] { return engine_->create(); });
    });
    add("file.open", "&File/&Open...", FL_CTRL + 'o', [this] {
      if (!discard())
        return;
      const auto path = choose(false);
      if (!path.empty()) {
        pending_path_ = path;
        start("open", [this, path] { return engine_->open(path); });
      }
    });
    add("file.saveAs", "&File/&Save As...", FL_CTRL + FL_SHIFT + 's', [this] {
      auto path = options_.save_path.empty() ? choose(true) : options_.save_path;
      if (!path.empty()) {
        pending_path_ = path;
        start("save", [this, path] { return engine_->save(path); });
      }
    });
    add("file.save", "&File/&Save", FL_CTRL + 's', [this] {
      auto path = options_.document.empty()
                      ? (options_.save_path.empty() ? choose(true) : options_.save_path)
                      : options_.document;
      if (!path.empty()) {
        pending_path_ = path;
        start("save", [this, path] { return engine_->save(path); });
      }
    });
    if (config_.at("name") == "gridcraft")
      add("home.insertSheet", "&Sheet/&New sheet", 0, [this] {
        start("new sheet", [this] { return engine_->execute("home.insertSheet", Json::object()); });
      });
    if (config_.at("name") == "deckcraft") {
      add("show.fromStart", "&Slide Show/From &beginning", 0, [this] {
        playing_ = true;
        app_.window().fullscreen();
        office_->presentation(true);
        start("show", [this] {
          engine_->execute("slide.first", Json::object());
          return engine_->show("start");
        });
      });
      add("show.fromCurrent", "&Slide Show/From &current slide", 0, [this] {
        playing_ = true;
        app_.window().fullscreen();
        office_->presentation(true);
        start("show", [this] { return engine_->show("start"); });
      });
    }
    if (config_.at("name") == "deckcraft")
      add("show.start", "&View/&Slide show", FL_F + 5, [this] {
        playing_ = true;
        app_.window().fullscreen();
        office_->presentation(true);
        start("show", [this] { return engine_->show("start"); });
      });
    if (config_.at("name") == "deckcraft")
      add("show.stop", "&View/&End slide show", 0, [this] {
        queued_.push_back({{"@domed.show", {{"action", "exit"}}}});
        if (!busy_)
          dispatch_queue();
      });
    add("file.print", "&File/&Print...", FL_CTRL + 'p',
        [this] { start("print", [this] { return engine_->print_pages(); }); });
    add("file.exportPdf", "&File/&Export PDF...", 0, [this] {
      auto path = choose(true, ".pdf");
      if (!path.empty())
        start("export PDF", [this, path] {
          return engine_->execute(config_.at("name") == "deckcraft" ? "file.export"
                                                                    : "file.exportPdf",
                                  {{"path", path.string()}, {"format", "pdf"}});
        });
    });
    add("view.previousPage", "&View/&Previous page", FL_ALT + FL_Page_Up, [this] {
      if (config_.at("name") == "deckcraft")
        start("slide", [this] { return engine_->execute("slide.previous", Json::object()); });
      else if (page_ > 0) {
        --page_;
        start("page", [] { return Json::object(); });
      }
    });
    add("view.nextPage", "&View/&Next page", FL_ALT + FL_Page_Down, [this] {
      if (config_.at("name") == "deckcraft")
        start("slide", [this] { return engine_->execute("slide.next", Json::object()); });
      else {
        ++page_;
        start("page", [] { return Json::object(); });
      }
    });
    if (config_.at("name") == "gridcraft")
      add("view.preview", "&View/&Page preview", 0, [this] {
        preview_mode_ = !preview_mode_;
        start("preview", [] { return Json::object(); });
      });
    if (config_.at("name") == "deckcraft")
      add("slide.new", "&Insert/&New slide", FL_CTRL + 'm', [this] {
        dirty_ = true;
        start("new slide", [this] { return engine_->execute("slide.new", Json::object()); });
      });
    add(
        "edit.cut", "&Edit/Cu&t", FL_CTRL + 'x',
        [this] {
          dirty_ = true;
          start("copy", [this] { return engine_->execute("edit.cut", Json::object()); });
        },
        "edit.cut");
    add("edit.copy", "&Edit/&Copy", FL_CTRL + 'c', [this] {
      start("copy", [this] { return engine_->execute("edit.copy", Json::object()); });
    });
    add("edit.paste", "&Edit/&Paste", FL_CTRL + 'v', [this] { office_->paste(); });
    add("edit.selectAll", "&Edit/Select &All", FL_CTRL + 'a', [this] {
      start("select", [this] {
        return engine_->execute(config_.at("name") == "wordcraft" ? "select.all" : "edit.selectAll",
                                Json::object());
      });
    });
    add("format.font", "&Format/&Font...", 0, [this] { choose_font(); });
    add("format.size", "&Format/&Font size...", 0, [this] {
      const char *value = fl_input("Font size in points", "12");
      if (!value)
        return;
      size_t read = 0;
      double size = std::stod(value, &read);
      if (read != std::string(value).size() || !std::isfinite(size) || size < 1 || size > 409)
        throw std::runtime_error("Font size must be from 1 to 409 points");
      auto command = config_.at("name") == "gridcraft" ? "home.fontSize" : "format.size";
      dirty_ = true;
      start("font size",
            [this, command, size] { return engine_->execute(command, {{"size", size}}); });
    });
    for (auto format : {std::pair{"bold", 'b'}, std::pair{"italic", 'i'}}) {
      const std::string command =
          (config_.at("name") == "gridcraft" ? "home." : "format.") + std::string(format.first);
      add(
          command.c_str(), format.second == 'b' ? "&Format/&Bold" : "&Format/&Italic",
          FL_CTRL + format.second,
          [this, command] {
            dirty_ = true;
            start(command, [this, command] { return engine_->execute(command, Json::object()); });
          },
          command.c_str());
    }
    add(
        "edit.undo", "&Edit/&Undo", FL_CTRL + 'z',
        [this] {
          dirty_ = true;
          start("undo", [this] { return engine_->execute("edit.undo", Json::object()); });
        },
        "edit.undo");
    add(
        "edit.redo", "&Edit/&Redo", FL_CTRL + FL_SHIFT + 'z',
        [this] {
          dirty_ = true;
          start("redo", [this] { return engine_->execute("edit.redo", Json::object()); });
        },
        "edit.redo");
    add("edit.discard", "&Edit/&Discard text edit", 0, [this] {
      office_->discard_pending();
      office_->update(snapshot_, false);
    });
    add("edit.apply", "&Edit/&Apply text edit", FL_CTRL + FL_Enter, [this] { office_->commit(); });
    add("engine.run", mac ? "&Tools/Command workbench/&Run selected" : "&Commands/&Run selected", 0,
        [this] { execute_selected(); });
    add("document.refresh", "&View/&Refresh", 0,
        [this] { start("refresh", [] { return Json::object(); }); });
    add("app.close", "&File/&Close", FL_CTRL + 'w', [this] {
      if (discard())
        app_.quit();
    });
    add("app.quit", mac ? ("&" + title + "/&Quit").c_str() : "&File/&Quit", FL_CTRL + 'q', [this] {
      if (discard())
        app_.quit();
    });
    if (options_.tray) {
      add("window.restore", "&Window/&Restore", 0, [this] { app_.show(); });
      add("window.hide", "&Window/&Hide to tray", 0, [this] { app_.hide_to_tray(); });
    }
    if (config_.contains("catalogue")) {
      auto path = options_.registry.parent_path() / config_.at("catalogue").get<std::string>();
      if (std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("Command catalogue exceeds 2 MiB");
      std::ifstream input(path);
      Json catalogue;
      input >> catalogue;
      std::set<std::string> paths = menu_paths;
      for (const auto &command : catalogue) {
        const auto id = command.at("id").get<std::string>();
        if (registered.contains(id))
          continue;
        auto safe = [](std::string value) {
          for (char &c : value)
            if (c == '/' || c == '&')
              c = ' ';
          return value;
        };
        std::string category;
        if (command.contains("menu") && command.at("menu").is_array())
          for (const auto &item : command.at("menu"))
            category += safe(item.get<std::string>()) + "/";
        else {
          category = command.value("location", std::string("Other"));
          size_t at = 0;
          while ((at = category.find(" › ", at)) != std::string::npos)
            category.replace(at, 5, "/");
          category += "/";
        }
        const auto label = command.value("label", id);
        auto menu = "&Tools/&All commands/" + category + safe(label);
        if (mac) {
          if (category.starts_with("Home/Font/"))
            category = "Format/Font/" + category.substr(10);
          else if (category.starts_with("Home/Paragraph/"))
            category = "Format/Paragraph/" + category.substr(15);
          else if (category.starts_with("Home/Styles/"))
            category = "Format/Styles/" + category.substr(12);
          else if (category.starts_with("Home/"))
            category = "Edit/" + category.substr(5);
          else if (category.starts_with("Table Layout/"))
            category = "Table/Layout/" + category.substr(13);
          else if (category.starts_with("Table Design/"))
            category = "Table/Design/" + category.substr(13);
          else if (category.starts_with("Picture Format/") ||
                   category.starts_with("Shape Format/") || category.starts_with("Chart Design/") ||
                   category.starts_with("SmartArt Design/") || category.starts_with("Equation/"))
            category = "Format/" + category;
          else if (category.starts_with("Navigation/") || category.starts_with("Editing/"))
            category = "Edit/" + category;
          else if (category.starts_with("Reading/") || category.starts_with("Status Bar/") ||
                   category.starts_with("Outlining/"))
            category = "View/" + category;
          else if (category.starts_with("Title Bar/"))
            category = "Window/" + category;
          else if (!category.starts_with("File/") && !category.starts_with("View/") &&
                   !category.starts_with("Insert/") && !category.starts_with("Data/") &&
                   !category.starts_with("Slide Show/"))
            category = "Tools/" + category;
          menu = "&" + category + safe(label);
        }
        auto original = menu;
        int number = 2;
        while (!paths.insert(normalized(menu)).second)
          menu = original + " (" + std::to_string(number++) + ")";
        int shortcut = 0;
        auto portable = command.contains("shortcut") && command.at("shortcut").is_string()
                            ? command.at("shortcut").get<std::string>()
                            : std::string();
        portable = portable.substr(0, portable.find(" / "));
        std::vector<std::string> parts;
        size_t at = 0;
        while (at < portable.size()) {
          auto end = portable.find('+', at);
          if (end == std::string::npos)
            end = portable.size();
          parts.push_back(portable.substr(at, end - at));
          at = end + 1;
        }
        for (const auto &part : parts) {
          if (part == "Mod" || part == "Cmd" || part == "Ctrl")
            shortcut |= FL_CTRL;
          else if (part == "Shift")
            shortcut |= FL_SHIFT;
          else if (part == "Alt" || part == "Opt")
            shortcut |= FL_ALT;
          else if (part.size() == 1)
            shortcut |= std::tolower(static_cast<unsigned char>(part[0]));
          else if (part == "Home")
            shortcut |= FL_Home;
          else if (part == "End")
            shortcut |= FL_End;
          else if (part == "PageUp")
            shortcut |= FL_Page_Up;
          else if (part == "PageDown")
            shortcut |= FL_Page_Down;
          else if (part == "Delete")
            shortcut |= FL_Delete;
          else if (part == "Backspace")
            shortcut |= FL_BackSpace;
          else if (part == "Enter" || part == "Return")
            shortcut |= FL_Enter;
          else if (part == "Tab")
            shortcut |= FL_Tab;
          else if (part == "Left")
            shortcut |= FL_Left;
          else if (part == "Right")
            shortcut |= FL_Right;
          else if (part == "Up")
            shortcut |= FL_Up;
          else if (part == "Down")
            shortcut |= FL_Down;
          else if (part.size() > 1 && part[0] == 'F') {
            try {
              int function = std::stoi(part.substr(1));
              if (function >= 1 && function <= 24)
                shortcut |= FL_F + function;
              else
                shortcut = 0;
            } catch (const std::exception &) {
              shortcut = 0;
            }
          } else {
            shortcut = 0;
            break;
          }
        }
        if (shortcuts.contains(shortcut))
          shortcut = 0;
        add(
            id.c_str(), menu.c_str(), shortcut,
            [this, id, command] {
              modal_ = true;
              std::optional<Json> params;
              try {
                params = dome_office::command_form(
                    "org.domed." + config_.at("title").get<std::string>(), command);
                modal_ = false;
              } catch (...) {
                modal_ = false;
                throw;
              }
              if (params) {
                if (command.value("mutates", true))
                  dirty_ = true;
                start(id, [this, id, params = *params] { return engine_->execute(id, params); });
              }
            },
            id.c_str());
      }
    }
    if (mac) {
      const std::vector<std::string> order = {title,     "File",       "Edit",   "View",  "Insert",
                                              "Format",  "Tools",      "Data",   "Sheet", "Table",
                                              "Arrange", "Slide Show", "Window", "Help"};
      auto rank = [&](const artcraft::Command &c) {
        auto path = normalized(c.menu_path);
        auto top = path.substr(0, path.find('/'));
        return std::find(order.begin(), order.end(), top) - order.begin();
      };
      std::stable_sort(native_commands.begin(), native_commands.end(),
                       [&](const auto &a, const auto &b) { return rank(a) < rank(b); });
    }
    for (auto &command : native_commands)
      app_.add_command(std::move(command));
    menu_commands_ = registered.size();
    selected_ = config_.at("smoke_command");
    start("startup", [this] {
      engine_ = std::make_unique<artcraft::UpstreamSession>(config_, options_.engine.string(),
                                                            options_.workspace);
      return options_.document.empty() ? engine_->create() : engine_->open(options_.document);
    });
    Fl::add_timeout(.1, tick_callback, this);
    if (options_.lifetime)
      Fl::add_timeout(options_.lifetime / 1000., close_callback, this);
  }
  ~Port() {
    Fl::remove_timeout(tick_callback, this);
    Fl::remove_timeout(close_callback, this);
    if (job_.valid())
      job_.wait();
    document_->buffer(nullptr);
  }
  int run() { return app_.run(); }
};
} // namespace
int dome_craft_port_main(const char *project_name, const char *registry_path,
                         const char *engine_directory, int argc, char **argv) {
  try {
    Options options;
    options.registry = registry_path;
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--help") {
        std::cout << project_name
                  << " DomeD upstream port [document] [--engine PATH] [--registry FILE] "
                     "[--workspace DIR] [--report FILE] [--capture FILE.ppm] [--save-path FILE] "
                     "[--tray] [--quit-after-ms N]\n";
        return 0;
      }
      if (argument == "--tray")
        options.tray = true;
      else if (argument == "--quit-after-ms" && index + 1 < argc) {
        const std::string value = argv[++index];
        size_t read = 0;
        options.lifetime = std::stoi(value, &read);
        if (read != value.size() || options.lifetime < 1 || options.lifetime > 600000)
          throw std::runtime_error("Invalid lifetime");
      } else if (index + 1 < argc &&
                 (argument == "--engine" || argument == "--registry" || argument == "--workspace" ||
                  argument == "--report" || argument == "--capture" || argument == "--save-path")) {
        const auto value = std::filesystem::absolute(argv[++index]);
        if (argument == "--engine")
          options.engine = value;
        if (argument == "--registry")
          options.registry = value;
        if (argument == "--workspace")
          options.workspace = value;
        if (argument == "--report")
          options.report = value;
        if (argument == "--capture")
          options.capture = value;
        if (argument == "--save-path")
          options.save_path = value;
      } else if (!argument.empty() && argument.front() != '-' && options.document.empty())
        options.document = std::filesystem::absolute(argument);
      else
        throw std::runtime_error("Unknown/incomplete option: " + argument);
    }
    std::ifstream input(options.registry);
    Json lock;
    input >> lock;
    std::optional<Json> selected;
    for (const auto &project : lock.at("projects"))
      if (project.at("name") == project_name)
        selected = project;
    if (!selected)
      throw std::runtime_error("Project is missing from the upstream lock");
    if (options.engine.empty())
      options.engine =
          std::filesystem::path(engine_directory) / selected->at("cli").get<std::string>();
    if (options.workspace.empty())
      options.workspace = options.document.empty() ? std::filesystem::current_path()
                                                   : options.document.parent_path();
    Port port(*selected, std::move(options));
    return port.run();
  } catch (const std::exception &error) {
    std::cerr << project_name << ": " << error.what() << '\n';
    return 1;
  }
}
