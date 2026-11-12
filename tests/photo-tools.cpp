// SPDX-License-Identifier: MIT
#define main photocraft_application_main
#include "../ports/photo-main.cpp"
#undef main
#include <stdexcept>
namespace {
struct PhotoHarness {
  static void check(bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  }
  static void wait(Photo &photo) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    do {
      Fl::wait(.02);
      if (!photo.busy_ && photo.queued_.empty()) {
        check(photo.error_.empty(), photo.error_.c_str());
        return;
      }
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Native PhotoCraft edit timed out");
  }
  static void event(Photo &photo, int event, double x, double y, int modifiers = 0,
                    int button = FL_LEFT_MOUSE) {
    photo.canvas_->geometry();
    auto p = photo.canvas_->paper();
    double scale = photo.canvas_->scale();
    Fl::e_x = p.x + int(std::round(x * scale));
    Fl::e_y = p.y + int(std::round(y * scale));
    Fl::e_state = modifiers | (event == FL_RELEASE ? 0 : FL_BUTTON1);
    Fl::e_keysym = FL_Button + button;
    Fl::e_clicks = 0;
    Fl::e_number = event;
    check(photo.canvas_->handle(event), "Canvas did not handle the pointer event");
  }
  static void drag(Photo &photo, double x, double y, double ex, double ey, int modifiers = 0) {
    event(photo, FL_PUSH, x, y, modifiers);
    event(photo, FL_DRAG, ex, ey, modifiers);
    // Release modifiers before button-up: selection mode must stay captured at
    // press.
    event(photo, FL_RELEASE, ex, ey);
    wait(photo);
  }
  static void run(const Json &config, const Options &options) {
    Photo photo(config, options);
    photo.app_.show();
    wait(photo);
    photo.start("new", [&] {
      return photo.engine_->tool("doc_new",
                                 {{"width", 96}, {"height", 96}, {"name", "Selection controls"}});
    });
    wait(photo);
    photo.zoom(3);
    photo.tools_.at("marquee")->do_callback();
    check(photo.tool_ == "marquee" && photo.selection_operation_->visible() &&
              photo.feather_->visible() && !photo.brush_opacity_->visible(),
          "Marquee still shows brush controls");
    drag(photo, 10, 10, 50, 40);
    check(photo.snapshot_["document"]["selectionBounds"] == Json::array({10, 10, 40, 30}),
          "Marquee drag has incorrect document bounds");
    drag(photo, 30, 20, 60, 50, FL_SHIFT | FL_ALT);
    check(photo.snapshot_["document"]["selectionBounds"] == Json::array({30, 20, 20, 20}),
          "Shift+Alt must intersect and survive modifier release");
    photo.execute("select.deselect");
    wait(photo);
    photo.tool_variant_->value(1);
    photo.tool_variant_->do_callback();
    drag(photo, 10, 10, 70, 70);
    auto preview = photo.snapshot_["document"]["selectionPreview"];
    check(preview.is_object(), "Actual selection mask preview is missing");
    check(preview["rows"][0].get<std::string>()[0] == '0',
          "Elliptical selection is drawn as a bounding box");
    check(photo.canvas_->selected(40, 40) && !photo.canvas_->selected(10, 10),
          "Mask-aware hit testing is incorrect");
    auto bounds = photo.snapshot_["document"]["selectionBounds"];
    drag(photo, 40, 40, 45, 45);
    auto moved = photo.snapshot_["document"]["selectionBounds"];
    check(moved[0] == bounds[0].get<int>() + 5 && moved[1] == bounds[1].get<int>() + 5,
          "Dragging inside marquee does not move its outline");
    photo.execute("select.deselect");
    wait(photo);
    photo.tool_variant_->value(0);
    photo.tool_variant_->do_callback();
    // Starting outside the image must not clamp a phantom selection onto its
    // edge.
    event(photo, FL_PUSH, -10, -10);
    event(photo, FL_RELEASE, 10, 10);
    wait(photo);
    check(!photo.snapshot_["document"]["hasSelection"].get<bool>(),
          "Outside-image click creates a selection");
    event(photo, FL_PUSH, 10, 10);
    event(photo, FL_DRAG, 50, 50);
    Fl::e_keysym = FL_Escape;
    Fl::e_number = FL_KEYDOWN;
    photo.canvas_->handle(FL_KEYDOWN);
    event(photo, FL_RELEASE, 50, 50);
    wait(photo);
    check(!photo.snapshot_["document"]["hasSelection"].get<bool>(),
          "Escape committed the cancelled drag");
    // Full canvas bounds include the final row and column.
    drag(photo, 0, 0, 96, 96);
    check(photo.snapshot_["document"]["selectionBounds"] == Json::array({0, 0, 96, 96}),
          "Marquee cannot reach last image row/column");
    photo.execute("select.deselect");
    wait(photo);
    photo.set_tool("lasso");
    photo.tool_variant_->value(1);
    photo.tool_variant_->do_callback();
    for (auto point : std::array<std::pair<int, int>, 3>{{{10, 10}, {60, 10}, {35, 60}}}) {
      event(photo, FL_PUSH, point.first, point.second);
      event(photo, FL_RELEASE, point.first, point.second);
    }
    check(!photo.busy_ && photo.canvas_->points_.size() == 3,
          "Polygonal lasso committed before completion");
    Fl::e_keysym = FL_Enter;
    Fl::e_number = FL_KEYDOWN;
    photo.canvas_->handle(FL_KEYDOWN);
    wait(photo);
    check(photo.snapshot_["document"]["hasSelection"].get<bool>(),
          "Polygonal lasso did not create selection");
    photo.execute("select.deselect");
    wait(photo);
    photo.set_tool("selection");
    check(photo.tolerance_->visible() && photo.contiguous_->visible() && !photo.feather_->visible(),
          "Wand options are not contextual");
    photo.tolerance_->value(0);
    drag(photo, 5, 5, 5, 5);
    check(photo.snapshot_["document"]["hasSelection"].get<bool>(),
          "Magic Wand is not bound to the engine");
    photo.execute("select.deselect");
    wait(photo);
    photo.tool_variant_->value(1);
    photo.tool_variant_->do_callback();
    check(photo.brush_size_->visible() && !photo.tolerance_->visible(),
          "Quick Selection does not show brush size");
    drag(photo, 12, 12, 18, 18);
    check(photo.snapshot_["document"]["hasSelection"].get<bool>(),
          "Quick Selection does not reach upstream engine");
    photo.execute("select.deselect");
    wait(photo);
    photo.set_tool("brush");
    auto revision = photo.snapshot_["document"]["revision"].get<unsigned long>();
    event(photo, FL_PUSH, 30, 30);
    event(photo, FL_RELEASE, 30, 30);
    event(photo, FL_PUSH, 60, 60);
    event(photo, FL_RELEASE, 60, 60);
    wait(photo);
    check(photo.snapshot_["document"]["revision"].get<unsigned long>() >= revision + 2,
          "Rapid brush gestures were dropped or merged");
    photo.set_tool("marquee");
    photo.app_.window().size(720, 500);
    photo.layout();
    check(!photo.workspace_->visible() && photo.options_bar_->w() == 720,
          "Compact tool options overlap workspace picker");
    photo.app_.request_capture((options.workspace / "photo-tools.ppm").string());
    Fl::wait(.3);
    photo.app_.quit();
    std::cout << "Native marquee/ellipse/intersection, mask feedback, "
                 "selection movement, cancel/bounds, polygon lasso, wand and "
                 "queued brush gestures passed\n";
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::runtime_error("photo-tools-test LOCK ENGINE WORKSPACE");
    Json lock;
    std::ifstream(argv[1]) >> lock;
    Json config;
    for (auto &entry : lock["projects"])
      if (entry["name"] == "photocraft")
        config = entry;
    Options options;
    options.registry = std::filesystem::absolute(argv[1]);
    options.engine = std::filesystem::absolute(argv[2]);
    options.workspace = std::filesystem::absolute(argv[3]);
    options.width = 1100;
    options.height = 760;
    PhotoHarness::run(config, options);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
