// SPDX-License-Identifier: MIT
#include "office-ui.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace dome_office {
struct OfficeUi::Impl {
  View &view;
  Fl_Group *status;
  Fl_Scroll *tabs, *tools;
  Fl_Box *count, *notice;
  Fl_Choice *fonts = nullptr, *styles = nullptr, *zoom;
  Fl_Input *size = nullptr;
  std::vector<std::string> families;
  std::vector<Fl_Menu_Button *> menus;
  struct Binding {
    Fl_Widget *widget;
    std::string id;
    Json params;
    std::function<void()> action;
  };
  std::vector<std::unique_ptr<Binding>> fixed, buttons;
  std::map<std::string, Fl_Button *> tab_buttons;
  std::vector<Json> catalogue;
  Json snapshot = Json::object();
  bool busy = true, ready = false;
  std::string active = "Home", context;
  static void clicked(Fl_Widget *, void *data) {
    auto action = static_cast<Binding *>(data)->action;
    action();
  }
  std::string mapped(const std::string &id) const {
    if (view.project_ == "gridcraft") {
      if (id == "format.font")
        return "home.fontName";
      if (id == "format.size")
        return "home.fontSize";
      if (id == "format.bold" || id == "format.italic" || id == "format.underline" ||
          id == "format.strikethrough")
        return "home." + id.substr(7);
      if (id == "format.growFont")
        return "home.increaseFontSize";
      if (id == "format.shrinkFont")
        return "home.decreaseFontSize";
      if (id == "para.align")
        return "home.justify";
      if (id.starts_with("para.align"))
        return "home." + id.substr(5);
    }
    if (view.project_ == "deckcraft") {
      if (id == "format.growFont")
        return "format.grow";
      if (id == "format.shrinkFont")
        return "format.shrink";
      if (id == "para.align")
        return "format.justify";
      if (id.starts_with("para."))
        return "format." + id.substr(5);
      if (id == "edit.find")
        return "text.find";
      if (id == "edit.replace")
        return "text.replace";
    }
    return id;
  }
  void run(const std::string &original, const Json &arguments) {
    const auto id = mapped(original);
    auto params = arguments;
    if (view.project_ == "deckcraft" && id == "format.font" && params.is_object() &&
        params.contains("name")) {
      params["family"] = params["name"];
      params.erase("name");
    }
    if (view.project_ != "wordcraft" && original == "para.align")
      params = Json::object();
    if (busy)
      return;
    if (view.project_ == "gridcraft" && id.starts_with("view.zoom")) {
      int percent = 100;
      auto doc = snapshot.value("document", Json::object());
      int sheet = doc.value("activeSheet", 0);
      auto sheets = doc.value("sheets", Json::array());
      if (sheet >= 0 && sheet < int(sheets.size()))
        percent = sheets[sheet].value("zoom", 100);
      if (id == "view.zoomIn")
        percent += 10;
      else if (id == "view.zoomOut")
        percent -= 10;
      else if (params.is_object())
        percent = params.value("value", percent);
      view.submit_({{"view.zoom", {{"percent", std::clamp(percent, 10, 400)}}}});
      return;
    }
    if (view.project_ == "deckcraft" && id.starts_with("view.zoom")) {
      double percent = view.zoom_ > 0 ? view.zoom_ * 100 : 100;
      if (id == "view.zoomIn")
        percent += 10;
      else if (id == "view.zoomOut")
        percent -= 10;
      else if (params.is_object())
        percent = params.value("value", percent);
      view.zoom_ = std::clamp(percent, 10., 500.) / 100;
      view.layout_editor();
      view.redraw();
      return;
    }
    if (id == "@find") {
      view.find_focus_ = true;
      view.submit_({{"view.navigationPane", {{"value", true}}}});
      return;
    }
    if (id == "@about") {
      fl_message("WordCraft\nNative DomeD office editor\nPinned Storytold engine; installed bitmap "
                 "fonts.");
      return;
    }
    if (params.is_null() && view.invoke_)
      view.invoke_(id);
    else if (!params.is_null())
      view.submit_({{id, params}});
    view.focus_document();
  }
  Fl_Button *button(int x, int y, int w, int h, const std::string &label, const std::string &id,
                    Json params = nullptr, bool permanent = false) {
    auto *b = new Fl_Button(x, y, w, h);
    b->copy_label(label.c_str());
    b->copy_tooltip(id.c_str());
    auto data =
        std::make_unique<Binding>(Binding{b, id, params, [this, id, params] { run(id, params); }});
    b->callback(clicked, data.get());
    (permanent ? fixed : buttons).push_back(std::move(data));
    return b;
  }
  Impl(View &v) : view(v) {
    tabs = new Fl_Scroll(v.x(), v.y(), v.w(), 32);
    tabs->type(Fl_Scroll::HORIZONTAL);
    tabs->scrollbar_size(3);
    tabs->end();
    tools = new Fl_Scroll(v.x(), v.y() + 32, v.w(), 94);
    tools->type(Fl_Scroll::HORIZONTAL);
    tools->scrollbar_size(8);
    tools->end();
    status = new Fl_Group(v.x(), v.y() + v.h() - 28, v.w(), 28);
    count = new Fl_Box(v.x() + 8, v.y() + v.h() - 28, 240, 28);
    count->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    notice = new Fl_Box(v.x() + 250, v.y() + v.h() - 28, v.w() - 580, 28);
    notice->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    button(v.x() + v.w() - 326, v.y() + v.h() - 26, 58, 24, "Page", "view.onePage", Json::object(),
           true);
    button(v.x() + v.w() - 264, v.y() + v.h() - 26, 70, 24, "Width", "view.pageWidth",
           Json::object(), true);
    button(v.x() + v.w() - 190, v.y() + v.h() - 26, 26, 24, "-", "view.zoomOut", Json::object(),
           true);
    zoom = new Fl_Choice(v.x() + v.w() - 160, v.y() + v.h() - 26, 112, 24);
    for (auto label :
         {"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%"})
      zoom->add(label);
    zoom->value(4);
    zoom->tooltip("Document zoom");
    zoom->callback(
        [](Fl_Widget *, void *p) {
          auto &s = *static_cast<Impl *>(p);
          s.run("view.zoom", {{"value", std::stoi(s.zoom->text())}});
        },
        this);
    button(v.x() + v.w() - 44, v.y() + v.h() - 26, 28, 24, "+", "view.zoomIn", Json::object(),
           true);
    status->end();
    if (view.project_ != "wordcraft")
      for (auto &binding : fixed)
        if (binding->id == "view.onePage" || binding->id == "view.pageWidth")
          binding->widget->hide();
    build_tabs();
  }
  std::vector<std::string> contexts() const {
    std::vector<std::string> result;
    if (view.project_ != "wordcraft")
      return result;
    auto sel = snapshot.value("document", Json::object()).value("selection", Json::object());
    auto focus = sel.value("focus", Json::object()), anchor = sel.value("anchor", Json::object());
    if (focus.value("path", Json::array()).size() > 1) {
      result.push_back("Table Design");
      result.push_back("Table Layout");
    }
    auto para = snapshot.value("view", Json::object())
                    .value("paragraph", Json::object())
                    .value("paragraph", Json::object());
    auto text = para.value("text", std::string());
    int a = anchor.value("off", 0), b = focus.value("off", 0), at = std::min(a, b);
    if (anchor.value("path", Json()) == focus.value("path", Json()) && std::abs(a - b) == 3 &&
        at >= 0 && at + 3 <= int(text.size()) && text.substr(at, 3) == "\xef\xbf\xbc") {
      int index = 0;
      size_t pos = 0;
      while ((pos = text.find("\xef\xbf\xbc", pos)) != std::string::npos && int(pos) < at) {
        ++index;
        pos += 3;
      }
      auto objects = para.value("objects", Json::array());
      if (index < int(objects.size())) {
        auto object = objects[index];
        auto type = object.value("type", std::string());
        if (type == "image")
          result.push_back("Picture Format");
        else if (type == "shape" || type == "canvas")
          result.push_back("Shape Format");
        else if (type == "math")
          result.push_back("Equation");
        else if (type == "graphic") {
          auto kind = object.value("graphic", Json::object()).value("type", std::string());
          if (kind == "chart")
            result.push_back("Chart Design");
          else if (kind == "smartArt")
            result.push_back("SmartArt Design");
        }
      }
    }
    return result;
  }
  void build_tabs() {
    auto *previous = Fl_Group::current();
    tabs->clear();
    tab_buttons.clear();
    tabs->begin();
    int x = tabs->x() + 6;
    std::vector<std::string> names = {"Home",   "Insert",     "Draw",     "Design",
                                      "Layout", "References", "Mailings", "Review",
                                      "View",   "Developer",  "Zotero"};
    if (view.project_ == "gridcraft")
      names = {"Home", "Insert", "Draw", "Page Layout", "Formulas",
               "Data", "Review", "View", "Automate"};
    if (view.project_ == "deckcraft")
      names = {"Home",       "Insert",     "Draw",   "Design", "Transitions",
               "Animations", "Slide Show", "Record", "Review", "View"};
    auto selection = view.project_ == "wordcraft" ? snapshot.value("document", Json::object())
                                                        .value("selection", Json::object())
                                                        .value("focus", Json::object())
                                                        .value("path", Json::array())
                                                  : Json::array();
    if (selection.size() > 1) {
      names.push_back("Table Design");
      names.push_back("Table Layout");
    }
    for (const auto &name : contexts())
      if (name != "Table Design" && name != "Table Layout")
        names.push_back(name);
    if (std::find(names.begin(), names.end(), active) == names.end())
      active = "Home";
    for (const auto &name : names) {
      int width = std::max(48, int(name.size()) * 8 + 20);
      auto *b = new Fl_Button(x, tabs->y(), width, 28);
      b->copy_label(name.c_str());
      b->type(FL_TOGGLE_BUTTON);
      b->value(name == active);
      b->callback(
          [](Fl_Widget *widget, void *p) {
            auto &s = *static_cast<Impl *>(p);
            s.active = widget->label();
            for (auto &[name, b] : s.tab_buttons)
              b->value(name == s.active);
            s.build_tools();
            s.view.redraw();
          },
          this);
      tab_buttons[name] = b;
      x += width + 2;
    }
    tabs->end();
    Fl_Group::current(previous);
  }
  void caption(const std::string &name, int x, int w) {
    auto *label = new Fl_Box(x, tools->y() + 64, w, 18);
    label->copy_label(name.c_str());
    label->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  }
  void menu(const std::string &group, int x, int width, bool launcher = false) {
    auto *menu = new Fl_Menu_Button(x, tools->y() + (launcher ? 64 : 4), width, launcher ? 18 : 58);
    menu->copy_label(launcher ? "" : group.c_str());
    menu->copy_tooltip(("All " + group + " commands").c_str());
    menus.push_back(menu);
    for (size_t i = 0; i < catalogue.size(); ++i) {
      auto &command = catalogue[i];
      auto location = command.value("location", std::string());
      std::string prefix = active + " › ";
      if (!(location == active ||
            (location.starts_with(prefix) && location.substr(prefix.size()).starts_with(group))))
        continue;
      std::string label = command.value("label", command.at("id").get<std::string>());
      for (char &c : label)
        if (c == '/' || c == '&')
          c = ' ';
      const std::string id = command.at("id");
      auto data =
          std::make_unique<Binding>(Binding{menu, id, nullptr, [this, id] { run(id, nullptr); }});
      menu->add(label.c_str(), 0, clicked, data.get(),
                command.value("enabled", false) ? 0 : FL_MENU_INACTIVE);
      buttons.push_back(std::move(data));
    }
    if (!launcher)
      caption(group, x, width);
  }
  void build_tools() {
    auto *previous = Fl_Group::current();
    tools->clear();
    buttons.clear();
    menus.clear();
    fonts = nullptr;
    size = nullptr;
    styles = nullptr;
    tools->begin();
    int x = tools->x() + 8, y = tools->y() + 4;
    if (active == "Home") {
      button(x, y, 60, 58, "Paste", "edit.paste");
      button(x + 64, y, 58, 27, "Cut", "edit.cut");
      button(x + 64, y + 31, 58, 27, "Copy", "edit.copy");
      caption("Clipboard", x, 122);
      menu("Clipboard", x + 102, 20, true);
      x += 134;
      fonts = new Fl_Choice(x, y, 168, 27);
      fonts->tooltip("Installed bitmap font family");
      families.clear();
      for (const auto &f :
           snapshot.value("bitmap", Json::object()).value("families", Json::array())) {
        families.push_back(f.get<std::string>());
        std::string label = f;
        for (char &c : label)
          if (c == '/' || c == '&')
            c = ' ';
        fonts->add(label.c_str());
      }
      fonts->callback(
          [](Fl_Widget *, void *p) {
            auto &s = *static_cast<Impl *>(p);
            int i = s.fonts->value();
            if (i >= 0 && i < int(s.families.size()))
              s.run("format.font", {{"name", s.families[i]}});
          },
          this);
      size = new Fl_Input(x + 172, y, 46, 27);
      size->when(FL_WHEN_ENTER_KEY_ALWAYS);
      size->tooltip("Font size in points; press Enter");
      size->callback(
          [](Fl_Widget *, void *p) {
            auto &s = *static_cast<Impl *>(p);
            try {
              std::string value = s.size->value();
              size_t n = 0;
              double v = std::stod(value, &n);
              if (n != value.size() || !std::isfinite(v) || v < 1 || v > 409)
                throw std::runtime_error("size");
              s.run("format.size", {{"size", v}});
            } catch (const std::exception &) {
              fl_alert("Font size must be from 1 to 409 points.");
            }
          },
          this);
      button(x, y + 31, 30, 27, "B", "format.bold", Json::object())->type(FL_TOGGLE_BUTTON);
      button(x + 34, y + 31, 30, 27, "I", "format.italic", Json::object())->type(FL_TOGGLE_BUTTON);
      button(x + 68, y + 31, 30, 27, "U", "format.underline", Json::object())
          ->type(FL_TOGGLE_BUTTON);
      button(x + 102, y + 31, 38, 27, "abc", "format.strikethrough", Json::object())
          ->type(FL_TOGGLE_BUTTON);
      button(x + 144, y + 31, 34, 27, "A+", "format.growFont", Json::object());
      button(x + 182, y + 31, 36, 27, "A-", "format.shrinkFont", Json::object());
      caption("Font", x, 218);
      menu("Font", x + 198, 20, true);
      x += 230;
      if (view.project_ == "gridcraft") {
        button(x, y, 74, 27, "Wrap", "home.wrapText", Json::object());
        button(x + 78, y, 82, 27, "Merge", "home.mergeCenter", Json::object());
        button(x + 164, y, 44, 27, "Mid", "home.alignMiddle", Json::object());
      } else {
        button(x, y, 74, 27, "Bullets", "para.bullets", Json::object());
        button(x + 78, y, 82, 27, "Numbers", "para.numbering", Json::object());
        button(x + 164, y, 44, 27, "->", "para.indent", Json::object());
      }
      button(x, y + 31, 48, 27, "Left", "para.alignLeft", Json::object());
      button(x + 52, y + 31, 52, 27, "Center", "para.alignCenter", Json::object());
      button(x + 108, y + 31, 48, 27, "Right", "para.alignRight", Json::object());
      button(x + 160, y + 31, 48, 27, "Both", "para.align", {{"value", "justify"}});
      caption(view.project_ == "gridcraft" ? "Alignment" : "Paragraph", x, 208);
      menu(view.project_ == "gridcraft" ? "Alignment" : "Paragraph", x + 188, 20, true);
      x += 220;
      if (view.project_ == "wordcraft") {
        styles = new Fl_Choice(x, y, 132, 27);
        for (auto label :
             {"Normal", "Title", "Subtitle", "Heading 1", "Heading 2", "Heading 3", "Quote"})
          styles->add(label);
        styles->tooltip("Paragraph style");
        styles->callback(
            [](Fl_Widget *, void *p) {
              auto &s = *static_cast<Impl *>(p);
              s.run("styles.apply", {{"style", s.styles->text()}});
            },
            this);
        button(x, y + 31, 132, 27, "Clear formatting", "format.clear", Json::object());
        caption("Styles", x, 132);
        menu("Styles", x + 112, 20, true);
        x += 144;
      } else if (view.project_ == "deckcraft") {
        button(x, y, 112, 27, "New slide", "slide.new");
        button(x, y + 31, 112, 27, "Layout...", "slide.layout");
        caption("Slides", x, 112);
        x += 124;
      }
      button(x, y, 82, 27, "Find", view.project_ == "wordcraft" ? "@find" : "edit.find",
             view.project_ == "wordcraft" ? Json(Json::object()) : Json());
      button(x, y + 31, 82, 27, "Replace", "edit.replace");
      caption("Editing", x, 82);
      x += 94;

    } else if (active == "File") {
      for (auto [label, id] : {std::pair{"New", "file.new"},
                               {"Open...", "file.open"},
                               {"Save", "file.save"},
                               {"Save as...", "file.saveAs"},
                               {"Print...", "file.print"},
                               {"Export PDF...", "file.exportPdf"}}) {
        button(x, y, 112, 58, label, id);
        x += 120;
      }
    } else if (active == "View" && view.project_ == "wordcraft") {
      button(x, y, 126, 27, "Navigation", "view.navigationPane", Json::object());
      button(x, y + 31, 126, 27, "Ruler", "view.ruler", Json::object());
      x += 138;
      for (auto [label, id] : {std::pair{"100%", "view.zoom100"},
                               {"One page", "view.onePage"},
                               {"Page width", "view.pageWidth"},
                               {"Multiple pages", "view.multiplePages"}}) {
        button(x, y, 112, 58, label, id, Json::object());
        x += 120;
      }
    }
    std::set<std::string> groups;
    for (const auto &command : catalogue) {
      auto location = command.value("location", std::string());
      const std::string prefix = active + " › ";
      if (location.starts_with(prefix)) {
        auto group = location.substr(prefix.size());
        group = group.substr(0, group.find(" › "));
        groups.insert(group);
      } else if (location == active)
        groups.insert(active);
    }
    if (active == "Home") {
      groups.erase("Clipboard");
      groups.erase("Font");
      groups.erase("Paragraph");
      if (view.project_ == "wordcraft")
        groups.erase("Styles");
      groups.erase("Editing");
      groups.erase("Alignment");
    }
    for (const auto &group : groups) {
      menu(group, x, std::clamp(int(group.size()) * 8 + 28, 112, 190));
      x += std::clamp(int(group.size()) * 8 + 28, 112, 190) + 12;
    }
    if (active == "Help") {
      auto *b = button(x, y, 160, 58, "About WordCraft", "@about", Json::object());
      (void)b;
    }
    tools->end();
    Fl_Group::current(previous);
    sync();
    tools->redraw();
    if (view.invoke_)
      view.invoke_("@appearance");
  }
  void sync() {
    std::map<std::string, bool> enabled;
    for (const auto &c : catalogue)
      enabled[c.at("id")] = c.value("enabled", false);
    auto fmt = snapshot.value("view", Json::object()).value("format", Json::object());
    for (auto *list : {&fixed, &buttons})
      for (auto &b : *list) {
        if (dynamic_cast<Fl_Menu_Button *>(b->widget))
          continue;
        const auto id = mapped(b->id);
        bool allowed = !busy && (b->id == "edit.paste" || !enabled.contains(id) || enabled[id]);
        if (allowed)
          b->widget->activate();
        else
          b->widget->deactivate();
        if (auto *button = dynamic_cast<Fl_Button *>(b->widget);
            button && button->type() == FL_TOGGLE_BUTTON) {
          std::string key =
              b->id == "format.strikethrough" ? "strike" : b->id.substr(b->id.find('.') + 1);
          button->value(fmt.value(key, false));
        }
      }
    for (auto *menu : menus) {
      if (busy)
        menu->deactivate();
      else
        menu->activate();
      for (int i = 0; i < menu->size(); ++i) {
        auto *data = static_cast<Binding *>(menu->menu()[i].user_data());
        if (data)
          menu->mode(i, enabled[data->id] ? 0 : FL_MENU_INACTIVE);
      }
    }
    if (fonts) {
      if (busy)
        fonts->deactivate();
      else
        fonts->activate();
      auto font = fmt.value("fontShownAs", Json());
      if (!font.is_string())
        font = fmt.value("font", Json());
      if (font.is_string()) {
        auto it = std::find(families.begin(), families.end(), font.get<std::string>());
        if (it == families.end()) {
          auto fallback = snapshot.value("bitmap", Json::object()).value("family", std::string());
          it = std::find(families.begin(), families.end(), fallback);
        }
        fonts->value(it == families.end() ? -1 : int(it - families.begin()));
      }
    }
    if (size && Fl::focus() != size) {
      auto n = fmt.value("size", Json());
      size->value(n.is_number() ? n.dump().c_str() : "");
    }
    if (size) {
      if (busy)
        size->deactivate();
      else
        size->activate();
    }
    if (styles) {
      auto style = fmt.value("styleName", Json());
      int found = style.is_string() ? styles->find_index(style.get<std::string>().c_str()) : -1;
      styles->value(found);
      if (busy)
        styles->deactivate();
      else
        styles->activate();
    }
  }
};
OfficeUi::OfficeUi(View &v) : impl_(std::make_unique<Impl>(v)) {}
OfficeUi::~OfficeUi() = default;
int OfficeUi::top() const { return 128; }
int OfficeUi::bottom() const { return 28; }
void OfficeUi::message(const std::string &text) { impl_->notice->copy_label(text.c_str()); }
void OfficeUi::layout() {
  auto &s = *impl_;
  auto &v = s.view;
  // FLTK moves fixed children with their parent; the trailing controls are anchored explicitly.
  s.tabs->resize(v.x(), v.y(), v.w(), 32);
  s.tools->resize(v.x(), v.y() + 32, v.w(), 94);
  s.status->resize(v.x(), v.y() + v.h() - 28, v.w(), 28);
  s.count->resize(v.x() + 8, v.y() + v.h() - 28, std::max(80, std::min(280, v.w() - 340)), 28);
  s.notice->resize(v.x() + 290, v.y() + v.h() - 28, std::max(1, v.w() - 630), 28);
  const int offsets[] = {326, 264, 190, 44};
  int i = 0;
  for (auto &b : s.fixed)
    if (b->id.starts_with("view.")) {
      b->widget->position(v.x() + v.w() - offsets[i++], v.y() + v.h() - 26);
    }
  s.zoom->resize(v.x() + v.w() - 160, v.y() + v.h() - 26, 112, 24);
}
void OfficeUi::update(const Json &snapshot, bool busy) {
  auto &s = *impl_;
  s.snapshot = snapshot;
  s.busy = busy;
  if (snapshot.contains("commands")) {
    s.catalogue = snapshot.at("commands").get<std::vector<Json>>();
    for (auto &c : s.catalogue)
      if (c.contains("menu")) {
        std::string location;
        for (auto part : c.at("menu")) {
          if (!location.empty())
            location += " › ";
          location += part.get<std::string>();
        }
        c["location"] = location;
      }
  }
  auto doc = snapshot.value("document", Json::object());
  auto fmt = snapshot.value("view", Json::object()).value("format", Json::object());
  int page =
      snapshot.value("view", Json::object()).value("raster", Json::object()).value("page", 0) + 1;
  auto value = "Page " + std::to_string(page) + " of " + std::to_string(doc.value("pages", 1)) +
               "   " + std::to_string(doc.value("words", 0)) + " words";
  s.count->copy_label(value.c_str());
  auto focus = s.view.project_ == "wordcraft" ? doc.value("selection", Json::object())
                                                    .value("focus", Json::object())
                                                    .value("path", Json::array())
                                              : Json::array();
  std::string context;
  for (auto &name : s.contexts())
    context += name + "/";
  if (!s.ready && !s.catalogue.empty()) {
    s.ready = true;
    s.build_tabs();
    s.build_tools();
  } else if (context != s.context) {
    auto previous = s.active;
    s.build_tabs();
    if (previous != s.active)
      s.build_tools();
  }
  s.context = context;
  s.sync();
}
} // namespace dome_office

void dome_office::OfficeUi::geometry(int page, double scale) {
  auto &s = *impl_;
  auto doc = s.snapshot.value("document", Json::object());
  std::string value;
  if (s.view.project_ == "gridcraft")
    value = doc.value("activeCell", std::string("A1")) + "   " +
            doc.value("mode", std::string("Ready"));
  else if (s.view.project_ == "deckcraft")
    value = "Slide " +
            std::to_string(doc.value("selection", Json::object()).value("slide", 0) + 1) + " of " +
            std::to_string(doc.value("slides", Json::array()).size());
  else
    value = "Page " + std::to_string(page + 1) + " of " + std::to_string(doc.value("pages", 1)) +
            "   " + std::to_string(doc.value("words", 0)) + " words";
  s.count->copy_label(value.c_str());
  auto percent = std::to_string(int(std::round(scale * 75))) + "%";
  int index = s.zoom->find_index(percent.c_str());
  if (index < 0)
    index = s.zoom->add(percent.c_str());
  s.zoom->value(index);
}

dome_office::Json dome_office::OfficeUi::state() const {
  Json names = Json::array();
  for (const auto &[name, button] : impl_->tab_buttons) {
    (void)button;
    names.push_back(name);
  }
  return {{"active_tab", impl_->active}, {"tabs", names}, {"application_menu", "universal"}};
}

void dome_office::OfficeUi::visible(bool value) {
  for (auto *widget :
       {static_cast<Fl_Widget *>(impl_->tabs),
        static_cast<Fl_Widget *>(impl_->tools), static_cast<Fl_Widget *>(impl_->status)}) {
    if (value)
      widget->show();
    else
      widget->hide();
  }
}
