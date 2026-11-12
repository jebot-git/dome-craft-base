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
  static Fl_Widget *find(Fl_Group &group, const std::string &label) {
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
  static void pointer(Photo &photo, int kind, int x, int y, int button = FL_LEFT_MOUSE) {
    Fl::e_x = x;
    Fl::e_y = y;
    Fl::e_keysym = FL_Button + button;
    Fl::e_state = kind == FL_PUSH ? (button == FL_RIGHT_MOUSE ? FL_BUTTON3 : FL_BUTTON1) : 0;
    Fl::e_number = kind;
    Fl::handle(kind, &photo.app_.window());
  }
  static void click_popup(Photo &photo, const std::string &label) {
    int index = -1;
    for (size_t i = 0; i < photo.context_->entries().size(); ++i)
      if (photo.context_->entries()[i].label == label)
        index = i;
    check(index >= 0, "Context action missing");
    auto *row = photo.context_->rows()[index];
    int x = row->x() + 20, y = row->y() + row->h() / 2;
    pointer(photo, FL_PUSH, x, y);
    pointer(photo, FL_RELEASE, x, y);
    Fl::wait(.05);
  }
  static void accept_dialog(void *) {
    auto *window = Fl::first_window();
    auto *apply = window ? find(*window, "Apply") : nullptr;
    check(apply, "Dedicated controller did not display an Apply button");
    apply->do_callback();
  }
  static void dialogs(void *) {
    auto *window = Fl::first_window();
    if (!window || std::string(window->label() ? window->label() : "") != "Image Size")
      throw std::runtime_error("Dedicated Image Size dialog missing");
    auto *width = dynamic_cast<Fl_Value_Input *>(find(*window, "Width (pixels)"));
    auto *height = dynamic_cast<Fl_Value_Input *>(find(*window, "Height (pixels)"));
    check(width && height, "Image Size is missing dimension controls");
    width->value(80);
    width->do_callback();
    check(height->value() == 80, "Image Size constrain proportions does not update height");
    auto *resample = dynamic_cast<Fl_Choice *>(find(*window, "Resample"));
    check(resample, "Dedicated resampling control missing");
    for (int event : {FL_PUSH, FL_RELEASE}) {
      Fl::e_x = resample->x() + resample->w() - 12;
      Fl::e_y = resample->y() + 12;
      Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
      Fl::e_state = event == FL_PUSH ? FL_BUTTON1 : 0;
      Fl::e_number = event;
      Fl::handle(event, window);
    }
    auto *popup = dome_photo::ContextMenu::existing(window);
    check(popup && popup->visible(), "Dedicated dialog dropdown did not open natively");
    auto *row = popup->rows()[1];
    for (int event : {FL_PUSH, FL_RELEASE}) {
      Fl::e_x = row->x() + 20;
      Fl::e_y = row->y() + row->h() / 2;
      Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
      Fl::e_state = event == FL_PUSH ? FL_BUTTON1 : 0;
      Fl::e_number = event;
      Fl::handle(event, window);
    }
    check(resample->value() == 1 && !popup->visible(),
          "Dedicated dialog dropdown did not commit selection");
    auto *apply = find(*window, "Apply");
    check(apply, "Native dialog Apply button missing");
    apply->do_callback();
  }
  struct Frame {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
    bool dark(int x, int y) const {
      auto p = (size_t(y) * width + x) * 3;
      return pixels[p] < 48 && pixels[p + 1] < 48 && pixels[p + 2] < 48;
    }
    bool light(int x, int y) const {
      auto p = (size_t(y) * width + x) * 3;
      return pixels[p] > 208 && pixels[p + 1] > 208 && pixels[p + 2] > 208;
    }
  };
  static Frame capture(Photo &photo, const std::filesystem::path &path) {
    std::filesystem::remove(path);
    photo.app_.request_capture(path.string());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!std::filesystem::exists(path) && std::chrono::steady_clock::now() < deadline)
      Fl::wait(.02);
    check(std::filesystem::exists(path), "GPU selection capture timed out");
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int maximum;
    Frame frame;
    in >> magic >> frame.width >> frame.height >> maximum;
    in.get();
    check(magic == "P6" && maximum == 255, "Invalid selection framebuffer capture");
    frame.pixels.resize(size_t(frame.width) * frame.height * 3);
    in.read(reinterpret_cast<char *>(frame.pixels.data()), frame.pixels.size());
    check(bool(in), "Truncated selection framebuffer capture");
    return frame;
  }
  static void visible_outline(Photo &photo, const Options &options, const std::string &name) {
    auto frame = capture(photo, options.workspace / (name + ".ppm"));
    auto paper = photo.canvas_->paper();
    double zoom = photo.canvas_->scale(), pixel = double(frame.width) / photo.app_.window().w();
    int left = int((paper.x + 12 * zoom) * pixel), right = int((paper.x + 48 * zoom) * pixel),
        top = int((paper.y + 10 * zoom) * pixel);
    int best_dark = 0, best_light = 0;
    for (int dy = -2; dy <= 2; ++dy) {
      int dark = 0, light = 0;
      for (int x = left; x < right; ++x) {
        dark += frame.dark(x, top + dy);
        light += frame.light(x, top + dy);
      }
      if (std::min(dark, light) > std::min(best_dark, best_light)) {
        best_dark = dark;
        best_light = light;
      }
    }
    int minimum = std::max(1, (right - left) / 8);
    check(best_dark >= minimum && best_light >= minimum,
          "Selection outline has no visible alternating black/white pixels");
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
    for (double zoom : {.5, 1., 3.}) {
      photo.zoom(zoom);
      visible_outline(photo, options, "selection-white-" + std::to_string(int(zoom * 100)));
    }
    photo.zoom(3);
    auto animated_before = capture(photo, options.workspace / "selection-animation-before.ppm");
    int phase = photo.canvas_->selection_phase_;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (photo.canvas_->selection_phase_ == phase && std::chrono::steady_clock::now() < deadline)
      Fl::wait(.02);
    auto animated_after = capture(photo, options.workspace / "selection-animation-after.ppm");
    auto animation_paper = photo.canvas_->paper();
    double pixels = double(animated_after.width) / photo.app_.window().w();
    int changes = 0;
    for (int x = int((animation_paper.x + 12 * 3) * pixels);
         x < int((animation_paper.x + 48 * 3) * pixels); ++x)
      for (int y = int((animation_paper.y + 10 * 3) * pixels) - 2;
           y <= int((animation_paper.y + 10 * 3) * pixels) + 2; ++y)
        changes += animated_before.dark(x, y) != animated_after.dark(x, y);
    check(changes > 8, "Selection ants do not animate in the GPU framebuffer");
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
    photo.start("new", [&] {
      return photo.engine_->tool("doc_new",
                                 {{"width", 96}, {"height", 96}, {"background", "black"}});
    });
    wait(photo);
    photo.zoom(3);
    photo.set_tool("marquee");
    drag(photo, 10, 10, 50, 40);
    for (double zoom : {.5, 1., 3.}) {
      photo.zoom(zoom);
      visible_outline(photo, options, "selection-black-" + std::to_string(int(zoom * 100)));
    }
    photo.start("new", [&] {
      return photo.engine_->tool("doc_new",
                                 {{"width", 96}, {"height", 96}, {"background", "white"}});
    });
    wait(photo);
    photo.zoom(3);
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
    // Drive the root FLTK event dispatcher: callbacks alone missed the broken
    // compositor popup path and right-click visibility regression.
    auto *marquee = photo.tools_.at("marquee");
    pointer(photo, FL_PUSH, marquee->x() + 10, marquee->y() + 10, FL_RIGHT_MOUSE);
    pointer(photo, FL_RELEASE, marquee->x() + 10, marquee->y() + 10, FL_RIGHT_MOUSE);
    check(photo.context_->visible(), "Tool flyout did not open through pointer dispatch");
    photo.app_.request_capture((options.workspace / "photo-native-context.ppm").string());
    for (int i = 0; i < 5; ++i)
      Fl::wait(.02);
    click_popup(photo, "Elliptical marquee");
    check(photo.variant() == 1 && !photo.context_->visible(),
          "Tool flyout did not select its subtool");
    auto *variant = photo.tool_variant_;
    pointer(photo, FL_PUSH, variant->x() + variant->w() - 12, variant->y() + 12);
    pointer(photo, FL_RELEASE, variant->x() + variant->w() - 12, variant->y() + 12);
    check(photo.context_->visible(),
          "Options dropdown did not open through native window dispatcher");
    click_popup(photo, "Rectangular marquee");
    check(photo.variant() == 0, "Options dropdown did not select its subtool");
    pointer(photo, FL_PUSH, marquee->x() + 10, marquee->y() + 10);
    auto held_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(450);
    while (std::chrono::steady_clock::now() < held_until)
      Fl::wait(.02);
    check(photo.context_->visible(), "Holding a grouped tool did not open its flyout");
    pointer(photo, FL_RELEASE, marquee->x() + 10, marquee->y() + 10);
    click_popup(photo, "Rectangular marquee");
    check(photo.variant() == 0, "Long-press flyout could not select a tool");
    photo.set_tool("brush");
    auto paper = photo.canvas_->paper();
    pointer(photo, FL_PUSH, paper.x + 30, paper.y + 30, FL_RIGHT_MOUSE);
    pointer(photo, FL_RELEASE, paper.x + 30, paper.y + 30, FL_RIGHT_MOUSE);
    check(photo.context_->visible() && photo.context_->entries()[0].label == "Brush settings...",
          "Canvas right-click is ignored");
    Fl::e_keysym = FL_Escape;
    photo.context_->handle(FL_KEYDOWN);
    check(!photo.context_->visible(), "Escape did not dismiss context menu");
    auto layer = photo.snapshot_["document"]["layers"][0];
    bool visible = layer.value("visible", true);
    int lx = photo.layers_->x() + 10, ly = photo.layers_->y() + 18;
    pointer(photo, FL_PUSH, lx, ly, FL_RIGHT_MOUSE);
    pointer(photo, FL_RELEASE, lx, ly, FL_RIGHT_MOUSE);
    wait(photo);
    check(photo.context_->visible(), "Layer context menu did not open");
    check(photo.snapshot_["document"]["layers"][0]["visible"] == visible,
          "Layer right-click toggled visibility");
    size_t count = photo.snapshot_["document"]["layers"].size();
    click_popup(photo, "Duplicate layer");
    wait(photo);
    check(photo.snapshot_["document"]["layers"].size() == count + 1,
          "Layer context action did not reach the engine");
    Fl::add_timeout(.15, dialogs);
    photo.control("image.imageSize");
    wait(photo);
    check(photo.snapshot_["document"]["width"] == 80 && photo.snapshot_["document"]["height"] == 80,
          "Image Size dedicated controls did not resize the image");
    photo.set_tool("pen");
    for (auto point : std::array<std::pair<int, int>, 3>{{{10, 10}, {50, 10}, {40, 50}}}) {
      event(photo, FL_PUSH, point.first, point.second);
      event(photo, FL_RELEASE, point.first, point.second);
    }
    Fl::e_keysym = FL_Enter;
    Fl::e_number = FL_KEYDOWN;
    photo.canvas_->handle(FL_KEYDOWN);
    wait(photo);
    check(photo.snapshot_["view"]["paths"].value("workPath", Json()).is_object(),
          "Pen did not create a path graphically");
    photo.set_tool("path");
    drag(photo, 10, 10, 15, 15);
    photo.set_tool("type");
    event(photo, FL_PUSH, 15, 15);
    event(photo, FL_RELEASE, 15, 15);
    check(photo.type_editor_->visible(), "Type tool did not open the inline editor");
    photo.type_editor_->value("Inline editing");
    photo.finish_type(false);
    check(!photo.type_editor_->visible(), "Inline type cancellation did not close the editor");
    event(photo, FL_PUSH, 15, 15);
    event(photo, FL_RELEASE, 15, 15);
    photo.type_editor_->value("Native type");
    size_t type_layers = photo.snapshot_["document"]["layers"].size();
    photo.finish_type(true);
    wait(photo);
    check(photo.snapshot_["document"]["layers"].size() == type_layers + 1,
          "Inline type commit did not create an upstream text layer");
    photo.set_tool("crop");
    check(photo.crop_commit_->visible(), "Crop does not expose commit/cancel controls");
    drag(photo, 10, 10, 60, 60);
    check(photo.snapshot_["document"]["width"] == 80 && photo.canvas_->crop_.is_array(),
          "Crop applies destructively before confirmation");
    photo.canvas_menu(paper.x + 10, paper.y + 10);
    click_popup(photo, "Crop");
    wait(photo);
    check(photo.snapshot_["document"]["width"] == 50 && photo.snapshot_["document"]["height"] == 50,
          "Crop context confirmation failed");
    for (auto adjustment :
         {"brightnessContrast", "levels", "curves", "exposure", "vibrance", "hueSaturation",
          "colorBalance", "blackWhite", "photoFilter", "invert", "posterize", "threshold"}) {
      std::string command = "layer.newAdjustmentLayer." + std::string(adjustment);
      if (std::string(adjustment) != "invert")
        Fl::add_timeout(.05, accept_dialog);
      photo.control(command);
      wait(photo);
    }
    Fl::add_timeout(.05, accept_dialog);
    photo.control("channel.new");
    wait(photo);
    check(!photo.snapshot_["document"]["channels"]["alpha"].empty(),
          "Dedicated New Channel control did not create an alpha channel");
    photo.channels_->value(photo.channel_names_.size());
    photo.control("channel.delete");
    wait(photo);
    check(photo.snapshot_["document"]["channels"]["alpha"].empty(),
          "Native alpha channel deletion did not target the selected channel");
    check(!photo.native_control("3d.newMeshFromLayer"),
          "Unimplemented action offers a command form fallback");
    std::cout << "Pointer-dispatched tool/canvas/layer menus, dedicated Image Size, graphical "
                 "pen/path and inline type controls passed\n";
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
