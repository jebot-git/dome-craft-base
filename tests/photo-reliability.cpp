// SPDX-License-Identifier: MIT
#define main photocraft_application_main
#include "../ports/photo-main.cpp"
#undef main
#include <numeric>
namespace {
struct PhotoHarness {
  static void require(bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  }
  static void wait(Photo &photo) {
    auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    do {
      Fl::wait(.02);
      if (!photo.busy_ && photo.queued_.empty()) {
        require(photo.error_.empty(), photo.error_.c_str());
        return;
      }
    } while (std::chrono::steady_clock::now() < limit);
    throw std::runtime_error("Photo reliability action timed out");
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
  static void accept(void *) {
    auto *window = Fl::first_window();
    auto *apply = window ? find(*window, "Apply") : nullptr;
    require(apply, "Dialog has no Apply button");
    apply->do_callback();
  }
  static void pointer(Fl_Window &window, int event, int x, int y) {
    Fl::e_x = x;
    Fl::e_y = y;
    Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
    Fl::e_state = event == FL_PUSH ? FL_BUTTON1 : 0;
    Fl::e_number = event;
    Fl::handle(event, &window);
  }
  template <class T> static T *widget(Fl_Group &group) {
    for (int i = 0; i < group.children(); ++i) {
      if (auto *result = dynamic_cast<T *>(group.child(i)))
        return result;
      if (auto *child = dynamic_cast<Fl_Group *>(group.child(i)))
        if (auto *result = widget<T>(*child))
          return result;
    }
    return nullptr;
  }
  static void pick(void *) {
    auto *window = Fl::first_window();
    auto *picker = window ? widget<dome_photo::ColorPicker>(*window) : nullptr;
    require(picker, "Foreground dialog has no native color picker");
    auto *plane = picker->child(0);
    for (int event : {FL_PUSH, FL_RELEASE})
      pointer(*window, event, plane->x() + plane->w() - 2, plane->y() + 1);
    require(picker->r() > .98 && picker->g() < .02 && picker->b() < .02,
            "Saturation/value pointer selection cannot change black to red");
    auto *hex = dynamic_cast<Fl_Input *>(find(*picker, "Hex"));
    require(hex, "Picker has no hex control");
    hex->value("#00ff00");
    hex->do_callback();
    require(picker->g() == 1 && picker->r() == 0, "Hex color does not update picker");
    auto *blue = dynamic_cast<Fl_Value_Input *>(find(*picker, "B"));
    require(blue, "Picker has no blue control");
    blue->value(255);
    blue->do_callback();
    require(picker->b() == 1 && std::string(hex->value()) == "#00ffff",
            "RGB numeric input does not update color and hex");
    accept(nullptr);
  }
  static void canvas_click(Photo &photo, double x, double y, int modifiers = 0) {
    photo.canvas_->geometry();
    auto paper = photo.canvas_->paper();
    for (int event : {FL_PUSH, FL_RELEASE}) {
      Fl::e_x = paper.x + int(x * photo.canvas_->scale());
      Fl::e_y = paper.y + int(y * photo.canvas_->scale());
      Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
      Fl::e_state = modifiers | (event == FL_PUSH ? FL_BUTTON1 : 0);
      Fl::e_number = event;
      Fl::handle(event, &photo.app_.window());
    }
    wait(photo);
  }
  static void rasterize(void *data) {
    auto *window = Fl::first_window();
    static int attempts = 0;
    if (!window || std::string(window->label()) != "Rasterize Layer") {
      require(++attempts < 400, "Queued painting did not request rasterization");
      Fl::repeat_timeout(.05, rasterize, data);
      return;
    }
    attempts = 0;
    auto *button = find(*window, data ? "Rasterize" : "Cancel");
    require(button, "Painting on type did not display rasterization confirmation");
    button->do_callback();
  }
  static void run(const Json &config, Options options) {
    Photo photo(config, options);
    int shortcuts = 0;
    photo.app_.add_command(
        {"qa.shortcut", "&QA/&Shortcut", FL_F + 12, false, [&] { ++shortcuts; }, {}});
    photo.app_.show();
    wait(photo);
    // Real greyscale and grey/alpha PNG previews must render and histogram safely.
    const char *greyscale = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAAAAADRSSBWAAAAC0lEQVR4nGNg+"
                            "A8AAQIBAEK+vGgAAAAASUVORK5CYII=";
    const char *grey_alpha = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAQAAABeK7cBAAAADUlEQVR4nGNgYPj/"
                             "HwADAgH/5ncLrgAAAABJRU5ErkJggg==";
    for (auto encoded : {greyscale, grey_alpha}) {
      GpuImage image;
      image.set(encoded);
      require(image.pixels()->d() == 4 && image.pixels()->array[4] == 255,
              "Greyscale preview was not normalized to GPU-compatible RGB/alpha");
      photo.canvas_->refresh({{"view", {{"raster", {{"png", encoded}}}}}});
    }
    require(photo.canvas_->histogram()[0] == 0 && photo.canvas_->histogram()[255] == 1,
            "Histogram counted transparent grey/alpha pixels");
    photo.start("new", [&] {
      return photo.engine_->tool("doc_new",
                                 {{"width", 96}, {"height", 80}, {"background", "white"}});
    });
    wait(photo);
    auto &bins = photo.canvas_->histogram();
    require(std::accumulate(bins.begin(), bins.end(), 0u) == 96 * 80 && bins[255] == 96 * 80,
            "Histogram does not match opaque white document");
    photo.execute("edit.fill", {{"contents", "color"}, {"color", "#ff0000"}});
    wait(photo);
    require(photo.canvas_->histogram()[53] == 96 * 80, "Histogram does not update after red fill");
    Fl::add_timeout(.05, accept);
    photo.control("image.imageSize");
    wait(photo);
    // Exercise the real window/global shortcut dispatch after the modal menu is destroyed.
    Fl::e_keysym = FL_F + 12;
    Fl::e_state = 0;
    Fl::e_number = FL_SHORTCUT;
    Fl::handle(FL_SHORTCUT, &photo.app_.window());
    require(shortcuts == 1, "Window shortcut stopped working after modal destruction");
    Fl::add_timeout(.05, pick);
    photo.foreground_dialog();
    wait(photo);
    require(photo.foreground_ == "#00ffff", "Color dialog did not apply to engine foreground");
    auto *tabs = photo.top_tabs_;
    for (int event : {FL_PUSH, FL_RELEASE})
      pointer(photo.app_.window(), event, tabs->x() + tabs->w() - 10, tabs->y() + 12);
    auto *popup = photo.context_;
    require(popup->visible(), "Dock overflow did not open native popup");
    int histogram = -1;
    for (size_t i = 0; i < popup->entries().size(); ++i)
      if (popup->entries()[i].label == "Histogram")
        histogram = int(i);
    require(histogram >= 0, "Histogram is missing from dock overflow");
    auto *row = popup->rows()[histogram];
    for (int event : {FL_PUSH, FL_RELEASE})
      pointer(photo.app_.window(), event, row->x() + 10, row->y() + row->h() / 2);
    require(tabs->value() == photo.histogram_panel_ && photo.histogram_->visible_r(),
            "Histogram panel could not be opened from native dock popup");
    photo.app_.request_capture((options.workspace / "reliability-histogram.ppm").string());
    Fl::wait(.1);
    auto sample_revision = photo.snapshot_["document"]["revision"];
    photo.set_tool("eyedropper");
    canvas_click(photo, 40, 40);
    require(photo.foreground_ == "#ff0000", "Eyedropper did not sample the red composite");
    require(photo.snapshot_["document"]["revision"] == sample_revision,
            "Eyedropper sampling changed the document history");
    photo.execute("tools.setColors", {{"foreground", "#000000"}});
    wait(photo);
    photo.set_tool("brush");
    canvas_click(photo, 40, 40, FL_ALT);
    require(photo.foreground_ == "#ff0000" &&
                photo.snapshot_["document"]["revision"] == sample_revision,
            "Alt-brush sampling painted or failed to set foreground");
    photo.set_tool("type");
    photo.begin_type(10, 30);
    photo.type_editor_->value("Keyboard survives");
    photo.finish_type(true);
    wait(photo);
    photo.set_tool("brush");
    auto revision = photo.snapshot_["document"]["revision"];
    Json stroke = {{"points", Json::array({Json::array({20, 40}), Json::array({60, 40})})},
                   {"size", 8},
                   {"color", "#000000"}};
    Fl::add_timeout(.05, rasterize);
    photo.execute("paint.stroke", stroke);
    wait(photo);
    require(photo.snapshot_["document"]["revision"] == revision,
            "Cancelling rasterization modified the document");
    Fl::add_timeout(.05, rasterize, &photo);
    photo.execute("paint.stroke", stroke);
    wait(photo);
    require(photo.snapshot_["document"]["revision"].get<unsigned long>() >=
                revision.get<unsigned long>() + 2,
            "Rasterize confirmation did not apply conversion and queued brush stroke");
    photo.execute("edit.undo");
    wait(photo);
    photo.execute("edit.undo");
    wait(photo);
    bool has_type = false;
    for (auto layer : photo.snapshot_["document"]["layers"])
      has_type = has_type || layer.value("kind", std::string()) == "Type";
    require(has_type, "Undo did not restore editable type after rasterize and paint");
    photo.set_tool("type");
    photo.begin_type(20, 50);
    photo.type_editor_->value("Queued type then paint");
    photo.finish_type(true);
    require(photo.busy_, "Type creation was not asynchronous");
    Fl::add_timeout(.05, rasterize, &photo);
    photo.execute("paint.stroke", stroke);
    photo.execute("paint.stroke", stroke);
    require(photo.queued_.size() == 2, "Rapid strokes were lost while type committed");
    wait(photo);
    Fl::e_keysym = FL_F + 12;
    Fl::e_state = 0;
    Fl::e_number = FL_SHORTCUT;
    Fl::handle(FL_SHORTCUT, &photo.app_.window());
    require(shortcuts == 2, "Repeated color and rasterization dialogs left invalid menu shortcuts");
    photo.app_.quit();
    std::cout << "Modal shortcuts, native colour picker, histogram dock/data, type, rasterization, "
                 "brush and undo passed\n";
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::runtime_error("photo-reliability-test LOCK ENGINE WORKSPACE");
    Json lock;
    std::ifstream(argv[1]) >> lock;
    Json config;
    for (auto c : lock["projects"])
      if (c["name"] == "photocraft")
        config = c;
    Options options;
    options.registry = std::filesystem::absolute(argv[1]);
    options.engine = std::filesystem::absolute(argv[2]);
    options.workspace = std::filesystem::absolute(argv[3]);
    PhotoHarness::run(config, options);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
