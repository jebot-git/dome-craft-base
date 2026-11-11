// SPDX-License-Identifier: MIT
#include <artcraft/application.hpp>
#include <artcraft/upstream_session.hpp>
#include "../ports/office-view.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Gl_Window.H>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Json = nlohmann::json;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
Fl_Widget *find(Fl_Group &group, const std::string &label) {
  for (int i = 0; i < group.children(); ++i) {
    auto *child = group.child(i);
    if (child->label() && child->label() == label)
      return child;
    if (auto *nested = dynamic_cast<Fl_Group *>(child))
      if (auto *found = find(*nested, label))
        return found;
  }
  return nullptr;
}
int main(int argc, char **argv) {
  try {
    require(argc == 4, "word-ui-test LOCK ENGINE WORKSPACE");
    std::ifstream file(argv[1]);
    Json lock;
    file >> lock;
    Json config;
    for (auto c : lock.at("projects"))
      if (c.at("name") == "wordcraft")
        config = c;
    artcraft::Application app({"org.domed.WordCraft.LayoutTest", "WordCraft", 1100, 760, false});
    artcraft::UpstreamSession engine(config, argv[2], argv[3]);
    engine.create();
    engine.execute("text.insert", {{"text", "DomeD office"}});
    engine.execute("styles.apply", {{"style", "Heading 1"}});
    engine.execute("text.newParagraph", Json::object());
    engine.execute("styles.apply", {{"style", "Normal"}});
    engine.execute("text.insert",
                   {{"text", "Write directly on the page. Bitmap fonts remain native to DomeD."}});
    engine.execute("insert.pageBreak", Json::object());
    engine.execute("text.insert", {{"text", "Second page: the document continues here."}});
    engine.execute("insert.pageBreak", Json::object());
    engine.execute("text.insert", {{"text", "Third page."}});
    engine.execute("caret.docStart", Json::object());
    std::string last;
    app.window().begin();
    dome_office::View view(0, app.content_top(), 1100, 730 - app.content_top(), "wordcraft",
                           [&](auto commands) {
                             for (const auto &[id, params] : commands) {
                               last = id;
                               if (id == "@domed.wordSelection")
                                 engine.select_text(params);
                               else if (id != "@domed.pagePreview")
                                 engine.execute(id, params);
                             }
                           });
    app.window().end();
    app.window().resizable(view);
    auto update = [&] {
      auto snapshot = engine.snapshot();
      snapshot["view"]["raster"] = engine.render(0);
      snapshot["view"]["rasters"] = Json::array();
      for (int i = 0; i < snapshot["document"].value("pages", 1); ++i)
        snapshot["view"]["rasters"].push_back(engine.render(i));
      view.update(snapshot, false);
    };
    update();
    app.show();
    Fl::wait(.1);
    auto state = view.ui_state();
    require(state.at("native_word_layout") && state.at("pages").size() == 3,
            "Word continuous page geometry is absent");
    require(!find(view, "File") && !find(view, "Help"),
            "Application menu duplicated in the ribbon");
    require(!state.at("paragraph_editor") && !state.at("navigation"),
            "Prototype tools occupy the default editor");
    auto *bold = dynamic_cast<Fl_Button *>(find(view, "B"));
    require(bold, "Native font controls absent");
    bold->do_callback();
    require(last == "format.bold" && engine.execute("format.state", Json::object()).at("bold"),
            "Font control does not change the upstream model");
    update();
    require(bold->value(), "Font control does not reflect engine state");
    // A ruler drag must alter the upstream paragraph, not just move a marker.
    auto *surface = find(view, "Document page surface");
    Fl_Widget *ruler = nullptr;
    for (int i = 0; i < view.children(); ++i) {
      auto *child = view.child(i);
      if (child->tooltip() && std::string(child->tooltip()).starts_with("Paragraph ruler:"))
        ruler = child;
    }
    require(ruler && surface, "Native paragraph ruler absent");
    auto first = state.at("pages").at(0);
    double original_scale = state.at("scale");
    Fl::e_x = surface->x() + int(first.at("x").get<double>() + 72 * original_scale);
    Fl::e_y = ruler->y() + ruler->h() - 4;
    Fl::e_number = FL_PUSH;
    Fl::e_state = FL_BUTTON1;
    require(ruler->handle(FL_PUSH), "Ruler indent marker did not accept a drag");
    Fl::e_x += int(18 * original_scale);
    Fl::e_number = FL_RELEASE;
    Fl::e_state = 0;
    ruler->handle(FL_RELEASE);
    auto paragraph = engine.snapshot().at("view").at("paragraph").at("resolved");
    require(last == "para.indents" && std::abs(paragraph.value("indentLeft", 0.) - 18) < 1,
            "Ruler drag did not update the paragraph indent");
    engine.execute("edit.undo", Json::object());
    update();
    // The same page-space point must select the same character at different zooms.
    Json focus;
    for (int percent : {75, 150}) {
      engine.execute("view.zoom", {{"value", percent}});
      update();
      auto ui = view.ui_state();
      auto p = ui.at("pages").at(0);
      auto *canvas = find(view, "Document page surface");
      require(canvas, "Page surface absent");
      auto *scroll = dynamic_cast<Fl_Scroll *>(canvas->parent());
      require(scroll, "Page scroll viewport absent");
      scroll->scroll_to(0, 0);
      double scale = ui.at("scale");
      Fl::e_x = canvas->x() + int(p.at("x").get<double>() + 100 * scale);
      Fl::e_y = canvas->y() + int(p.at("y").get<double>() + 80 * scale);
      Fl::e_state = FL_BUTTON1;
      Fl::e_clicks = 0;
      Fl::e_number = FL_PUSH;
      canvas->handle(FL_PUSH);
      Fl::e_state = 0;
      Fl::e_number = FL_RELEASE;
      canvas->handle(FL_RELEASE);
      auto selected = engine.execute("document.selection", Json::object()).at("focus");
      if (focus.is_null())
        focus = selected;
      else
        require(focus == selected, "Zoom changed page-space mouse selection");
    }
    engine.execute("view.zoom", {{"value", "pageWidth"}});
    engine.execute("view.navigationPane", {{"value", true}});
    update();
    require(view.ui_state().at("navigation"), "Navigation pane does not follow upstream state");
    for (auto size : {std::pair{720, 500}, std::pair{1100, 760}}) {
      app.window().size(size.first, size.second);
      Fl::wait(.1);
      auto ui = view.ui_state();
      auto rect = ui.at("canvas");
      require(rect.at("w").get<int>() > 200 && rect.at("h").get<int>() > 150,
              "Ribbon leaves no usable page viewport");
      require(rect.at("x").get<int>() + rect.at("w").get<int>() <= size.first,
              "Page viewport overflows constrained window");
      auto p = ui.at("pages").at(0);
      require(p.at("x").get<double>() >= 19 &&
                  p.at("w").get<double>() + 40 <= rect.at("w").get<double>(),
              "Fit-width page lacks centered canvas margins");
    }
    engine.execute("view.navigationPane", {{"value", false}});
    engine.execute("view.zoom", {{"value", "onePage"}});
    update();
    view.focus_document();
    app.request_capture(
        (std::filesystem::path(argv[3]) / "wordcraft-upstream-layout.ppm").string());
    for (int i = 0; i < 15; ++i)
      Fl::wait(.03);
    std::cout << "Native ribbon formatting/state, ruler editing, continuous pages, zoom hit "
                 "testing, navigation and 720/1100px layout passed\n";
    app.quit();
    app.window().remove(view);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
