// SPDX-License-Identifier: MIT
#include <artcraft/application.hpp>
#include <artcraft/upstream_session.hpp>
#include "../ports/office-view.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Multiline_Input.H>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Json = nlohmann::json;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::runtime_error("office-view-test LOCK ENGINES WORKSPACE");
    std::ifstream input(argv[1]);
    Json lock;
    input >> lock;
    for (const auto &config : lock.at("projects")) {
      if (config.value("suite", std::string()) != "office")
        continue;
      std::string name = config.at("name");
      artcraft::Application app({"org.domed." + config.at("title").get<std::string>(),
                                 "Office native edit verification", 900, 700, false});
      artcraft::UpstreamSession engine(
          config, (std::filesystem::path(argv[2]) / config.at("cli").get<std::string>()).string(),
          argv[3]);
      engine.create();
      if (name == "wordcraft")
        engine.execute("text.insert",
                       {{"text", "First paragraph\nKeep this paragraph"}, {"paragraphs", true}});
      if (name == "gridcraft")
        engine.execute("cell.set", {{"cell", "B2"}, {"input", "=SUM(1,2,3)"}});
      if (name == "gridcraft") {
        engine.execute("home.bold", {{"range", "B2"}, {"on", true}});
        engine.execute("home.fontColor", {{"range", "B2"}, {"color", "#FF0000"}});
        engine.execute("home.fillColor", {{"range", "B2"}, {"color", "#FFFF99"}});
      }
      if (name == "gridcraft")
        engine.execute("selection.set", {{"range", "A1"}});
      if (name == "wordcraft")
        engine.execute("caret.set",
                       {{"pos", {{"path", Json::array({0})}, {"off", 0}, {"story", "body"}}}});
      auto before = engine.snapshot();
      if (name == "gridcraft") {
        auto cells = before["view"]["cells"];
        auto key = cells["styles"][1][1].dump();
        require(cells["stylePalette"][key]["font"]["bold"] &&
                    cells["stylePalette"][key]["foreground"] == "#FF0000" &&
                    cells["stylePalette"][key]["background"] == "#FFFF99",
                "Styled viewport did not preserve cell formatting");
      }

      app.window().begin();
      dome_office::View view(0, app.content_top(), 900, 650, name, [&](auto commands) {
        for (const auto &[id, p] : commands)
          engine.execute(id, p);
      });
      app.window().end();
      view.update(before, false);
      Fl_Multiline_Input *editor = nullptr;
      for (int i = 0; i < view.children(); ++i)
        if (auto *candidate = dynamic_cast<Fl_Multiline_Input *>(view.child(i)))
          editor = candidate;
      require(editor, "Native text editing control is absent");
      std::string content = name == "gridcraft" ? "=SUM(4,5,6)" : "Bitmap office café 日本語";
      editor->value(content.c_str());
      editor->do_callback();
      require(view.pending(), "Unapplied edits must be tracked");
      view.commit();
      auto after = engine.snapshot();
      if (name == "wordcraft") {
        require(after["document"]["blocks"][0]["text"] == content,
                "Paragraph edit did not reach Word engine");
        require(after["document"]["blocks"][1]["text"] == "Keep this paragraph",
                "Paragraph edit damaged following content");
      } else if (name == "gridcraft") {
        require(after["view"]["inputs"]["values"][0][0] == "=SUM(4,5,6)" &&
                    (after["view"]["cells"]["values"][0][0] == 15. ||
                     after["view"]["cells"]["values"][0][0] == "15"),
                "Formula did not recalculate in Grid engine");
        require((after["view"]["cells"]["values"][1][1] == 6. ||
                 after["view"]["cells"]["values"][1][1] == "6"),
                "Formula edit damaged another cell");
      } else
        require(after["view"]["slide"]["shapes"][0]["text"] == content,
                "Shape text edit did not reach Deck engine");
      engine.execute("edit.undo", Json::object());
      auto undone = engine.snapshot();
      require(undone["document"] != after["document"], "Native edit has no upstream undo");
      engine.execute("edit.redo", Json::object());
      auto path = std::filesystem::path(argv[3]) /
                  (name + config.at("default_extension").get<std::string>());
      engine.save(path);
      engine.open(path);
      auto reopened = engine.snapshot();
      if (name == "wordcraft")
        require(reopened["document"]["blocks"][0]["text"] == content,
                "DOCX did not preserve native edit");
      if (name == "gridcraft")
        require((reopened["view"]["cells"]["values"][0][0] == 15. ||
                 reopened["view"]["cells"]["values"][0][0] == "15"),
                "XLSX did not preserve native edit");
      if (name == "deckcraft")
        require(reopened["view"]["slide"]["shapes"][0]["text"] == content,
                "PPTX did not preserve native edit");
      auto pdf = std::filesystem::path(argv[3]) / (name + "-bitmap.pdf");
      engine.execute(name == "deckcraft" ? "file.export" : "file.exportPdf",
                     {{"path", pdf.string()}, {"format", "pdf"}});
      require(std::filesystem::file_size(pdf) > 100, "Bitmap PDF export is empty");
      if (name == "wordcraft") {
        engine.select_text({{"from", {{"page", 0}, {"x", 72}, {"y", 80}}},
                            {"to", {{"page", 0}, {"x", 108}, {"y", 80}}}});
        auto selection = engine.snapshot();
        require(!selection["document"]["selection"]["text"].get<std::string>().empty(),
                "Mouse selection did not select engine text");
        require(!selection["view"]["layout"]["selection"].empty(),
                "Native selection geometry is missing");
      }
      auto raster = engine.render();
      require(raster.contains("png") && !raster.at("png").get<std::string>().empty(),
              "Bitmap page rendering is empty");
      auto pages = engine.print_pages();
      require(!pages.empty() && pages.at(0).contains("png"), "Bitmap print path has no pages");
      require(engine.bitmap_status().value("provider", std::string()) == "domed-bdf-v1" &&
                  !engine.bitmap_status().value("outlineFallback", true),
              "Engine bitmap-only provider is not active");
      if (name == "gridcraft") {
        engine.execute("cell.set", {{"cell", "XFD1048576"}, {"input", "=SUM(4,5,6)"}});
        engine.viewport(1048575, 16383);
        auto distant = engine.snapshot();
        require(distant["view"]["inputs"]["values"][0][0] == "=SUM(4,5,6)",
                "Full sheet viewport did not reach the last cell");
        engine.viewport(0, 0);
        engine.open(path);
      }
      reopened["view"]["raster"] = raster;
      view.discard_pending();
      view.update(reopened, false);
      app.show();
      Fl::wait(.1);
      app.request_capture((std::filesystem::path(argv[3]) / (name + "-native-controls.ppm")).string());
      Fl::wait(.2);
      auto info = app.platform_info();
      require(!info.font_file.empty(), "BDF font identity missing");
      require(editor->textfont() == editor->labelfont(),
              "Editor did not inherit the DomeD bitmap font");
      std::cout << name
                << ": native editing, preservation, undo/redo, Office round trip, bitmap "
                   "rendering/PDF and print pages passed\n";
      app.quit();
      app.window().remove(view);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
