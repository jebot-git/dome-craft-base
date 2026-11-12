// SPDX-License-Identifier: MIT
// PhotoCraft domain controllers. Never infer a UI from command parameter text.
bool native_control(const std::string &id) const {
  static const std::set<std::string> dedicated = {"layer.new.layer",
                                                  "layer.new.group",
                                                  "layer.duplicate",
                                                  "layer.delete",
                                                  "image.imageSize",
                                                  "image.canvasSize",
                                                  "layer.setProps",
                                                  "layer.layerStyle.blendingOptions",
                                                  "channel.new",
                                                  "channel.delete",
                                                  "path.stroke",
                                                  "path.fill",
                                                  "path.toSelection",
                                                  "path.transform",
                                                  "select.modify.feather",
                                                  "select.modify.expand",
                                                  "select.modify.contract",
                                                  "select.transformSelection",
                                                  "filter.blur.gaussianBlur",
                                                  "edit.fill",
                                                  "edit.stroke"};
  if (dedicated.contains(id))
    return true;
  if (id.starts_with("image.adjustments.") || id.starts_with("layer.newAdjustmentLayer.")) {
    auto name = id.substr(id.rfind('.') + 1);
    static const std::set<std::string> adjustments = {
        "brightnessContrast", "levels",     "curves",      "exposure", "vibrance",  "hueSaturation",
        "colorBalance",       "blackWhite", "photoFilter", "invert",   "posterize", "threshold"};
    return adjustments.contains(name);
  }
  auto found = catalogue_.find(id);
  return found != catalogue_.end() &&
         found->second.value("params", std::string()).starts_with("{}");
}
void control(const std::string &id, Json explicit_params = nullptr) {
  if (explicit_params.is_object()) {
    execute(id, std::move(explicit_params));
    return;
  }
  using dome_photo::Dialog;
  Json params = Json::object();
  auto doc = snapshot_.value("document", Json::object());
  if (id == "image.imageSize" || id == "image.canvasSize") {
    bool canvas = id == "image.canvasSize";
    Dialog dialog(canvas ? "Canvas Size" : "Image Size", 460);
    auto *width = dialog.number("Width (pixels)", doc.value("width", 1), 1, 30000);
    auto *height = dialog.number("Height (pixels)", doc.value("height", 1), 1, 30000);
    if (canvas) {
      auto *anchor = dialog.choice("Anchor",
                                   {"Top left", "Top", "Top right", "Left", "Center", "Right",
                                    "Bottom left", "Bottom", "Bottom right"},
                                   4);
      auto *extension = dialog.choice(
          "Canvas extension", {"Background", "Foreground", "White", "Black", "Transparent"});
      if (!dialog.run())
        return;
      static const char *anchors[] = {"topLeft", "top",        "topRight", "left",       "center",
                                      "right",   "bottomLeft", "bottom",   "bottomRight"};
      static const char *fills[] = {"background", "foreground", "white", "black", "transparent"};
      params = {{"width", width->value()},
                {"height", height->value()},
                {"anchor", anchors[anchor->value()]},
                {"extensionColor", fills[extension->value()]}};
    } else {
      auto *resolution =
          dialog.number("Resolution (ppi)", doc.value("resolution", 72.), 1, 9600, .1);
      auto *constrain = dialog.check("Constrain proportions", true);
      auto *sampling =
          dialog.choice("Resample", {"Bicubic", "Nearest neighbor", "Bilinear", "Lanczos"});
      struct Ratio {
        Fl_Value_Input *w, *h;
        Fl_Check_Button *lock;
        double ratio;
      };
      Ratio ratio{width, height, constrain,
                  double(doc.value("width", 1)) / std::max(1, doc.value("height", 1))};
      auto changed = [](Fl_Widget *widget, void *pointer) {
        auto &r = *static_cast<Ratio *>(pointer);
        if (!r.lock->value())
          return;
        if (widget == r.w)
          r.h->value(std::max(1., std::round(r.w->value() / r.ratio)));
        else
          r.w->value(std::max(1., std::round(r.h->value() * r.ratio)));
      };
      width->callback(changed, &ratio);
      height->callback(changed, &ratio);
      if (!dialog.run())
        return;
      static const char *samples[] = {"bicubic", "nearest", "bilinear", "lanczos"};
      params = {{"width", width->value()},
                {"height", height->value()},
                {"resolution", resolution->value()},
                {"resample", samples[sampling->value()]}};
    }
  } else if (id == "channel.new") {
    Dialog dialog("New Channel");
    auto *name = dialog.text("Name", "Alpha 1");
    auto *fill = dialog.choice("Initial contents",
                               {"Black (masked)", "White (selected)", "Current selection"});
    auto *opacity = dialog.number("Overlay opacity (%)", 50, 0, 100);
    auto *indicates = dialog.choice("Color indicates", {"Masked areas", "Selected areas"});
    if (!dialog.run())
      return;
    static const char *fills[] = {"black", "white", "selection"};
    params = {{"name", name->value()},
              {"fill", fills[fill->value()]},
              {"opacity", opacity->value()},
              {"indicates", indicates->value() ? "selected" : "masked"}};
  } else if (id == "channel.delete") {
    int row = channels_->value() - 1;
    int color_channels =
        doc.value("channels", Json::object()).value("colors", Json::array()).size();
    if (row <= color_channels || row >= int(channel_names_.size()))
      return;
    params = {{"channel", channel_names_[row]}};
  } else if (id == "layer.setProps") {
    Json layer;
    for (auto item : doc.value("layers", Json::array()))
      if (item.value("id", 0ul) == doc.value("activeLayer", 0ul))
        layer = item;
    Dialog dialog("Layer Properties");
    auto *name = dialog.text("Name", layer.value("name", std::string("Layer")));
    auto *opacity = dialog.number("Opacity (%)", layer.value("opacity", 1.) * 100, 0, 100);
    auto *visible = dialog.check("Visible", layer.value("visible", true));
    auto *locked = dialog.check("Lock layer", layer.value("locked", false));
    if (!dialog.run())
      return;
    params = {{"name", name->value()},
              {"opacity", opacity->value() / 100},
              {"visible", bool(visible->value())},
              {"locked", bool(locked->value())}};
  } else if (id == "layer.layerStyle.blendingOptions") {
    Dialog dialog("Blending Options");
    auto *mode = dialog.choice("Blend mode", {"Normal", "Multiply", "Screen", "Overlay", "Darken",
                                              "Lighten", "Difference", "Color", "Luminosity"});
    auto *opacity = dialog.number("Opacity (%)", layer_opacity_->value(), 0, 100);
    auto *fill = dialog.number("Fill opacity (%)", 100, 0, 100);
    auto *knockout = dialog.choice("Knockout", {"None", "Shallow", "Deep"});
    if (!dialog.run())
      return;
    static const char *modes[] = {"normal",  "multiply",   "screen", "overlay",   "darken",
                                  "lighten", "difference", "color",  "luminosity"};
    params = {
        {"blend", modes[mode->value()]},
        {"opacity", opacity->value()},
        {"fillOpacity", fill->value()},
        {"knockout", std::array<const char *, 3>{"none", "shallow", "deep"}[knockout->value()]}};
  } else if (id == "path.transform" || id == "select.transformSelection") {
    Dialog dialog(id == "path.transform" ? "Transform Path" : "Transform Selection");
    auto *dx = dialog.number("Horizontal offset (px)", 0, -30000, 30000);
    auto *dy = dialog.number("Vertical offset (px)", 0, -30000, 30000);
    auto *angle = dialog.number("Rotation (degrees)", 0, -360, 360, .1);
    auto *scale = dialog.number("Scale (%)", 100, 1, 10000, .1);
    if (!dialog.run())
      return;
    if (id == "path.transform")
      params = {{"name", selected_path()},        {"translateX", dx->value()},
                {"translateY", dy->value()},      {"angle", angle->value()},
                {"scaleX", scale->value() / 100}, {"scaleY", scale->value() / 100}};
    else
      params = {{"dx", dx->value()},
                {"dy", dy->value()},
                {"rotate", angle->value()},
                {"scaleX", scale->value()},
                {"scaleY", scale->value()}};
  } else if (id == "path.stroke") {
    Dialog dialog("Stroke Path");
    auto *size = dialog.number("Brush diameter (px)", brush_size_->value(), .5, 5000, .5);
    auto *pressure = dialog.check("Simulate pressure", false);
    if (!dialog.run())
      return;
    params = {{"name", selected_path()},
              {"size", size->value()},
              {"color", color()},
              {"simulatePressure", bool(pressure->value())}};
  } else if (id == "path.fill" || id == "path.toSelection") {
    params = {{"name", selected_path()}};
    if (id == "path.fill")
      params["color"] = color();
  } else if (id.starts_with("select.modify.") || id == "filter.blur.gaussianBlur") {
    Dialog dialog(id == "filter.blur.gaussianBlur" ? "Gaussian Blur"
                  : id.ends_with("feather")        ? "Feather Selection"
                  : id.ends_with("expand")         ? "Expand Selection"
                                                   : "Contract Selection",
                  220);
    auto *radius = dialog.number("Radius (pixels)", 1, .1, 1000, .1);
    if (!dialog.run())
      return;
    params = {{"radius", radius->value()}};
  } else if (id == "edit.fill") {
    Dialog dialog("Fill");
    auto *contents = dialog.choice("Contents", {"Foreground color", "Background color", "White",
                                                "Black", "50% gray", "Content-aware"});
    auto *opacity = dialog.number("Opacity (%)", 100, 0, 100);
    auto *preserve = dialog.check("Preserve transparency", false);
    if (!dialog.run())
      return;
    static const char *values[] = {"foreground", "background", "white",
                                   "black",      "gray",       "contentAware"};
    params = {{"contents", values[contents->value()]},
              {"opacity", opacity->value()},
              {"preserveTransparency", bool(preserve->value())}};
  } else if (id == "edit.stroke") {
    Dialog dialog("Stroke");
    auto *width = dialog.number("Width (pixels)", 1, 1, 250);
    auto *where = dialog.choice("Location", {"Inside", "Center", "Outside"}, 1);
    auto *opacity = dialog.number("Opacity (%)", 100, 0, 100);
    if (!dialog.run())
      return;
    params = {
        {"width", width->value()},
        {"color", color()},
        {"location", std::array<const char *, 3>{"inside", "center", "outside"}[where->value()]},
        {"opacity", opacity->value()}};
  } else if (id.starts_with("image.adjustments.") || id.starts_with("layer.newAdjustmentLayer.")) {
    auto name = id.substr(id.rfind('.') + 1);
    if (name != "invert") {
      Dialog dialog(catalogue_.at(id).value("label", std::string("Adjustment")), 480);
      if (name == "brightnessContrast") {
        auto *b = dialog.number("Brightness", 0, -150, 150);
        auto *c = dialog.number("Contrast", 0, -50, 100);
        auto *legacy = dialog.check("Use legacy", false);
        if (!dialog.run())
          return;
        params = {{"brightness", b->value()},
                  {"contrast", c->value()},
                  {"legacy", bool(legacy->value())}};
      } else if (name == "hueSaturation") {
        auto *h = dialog.number("Hue", 0, -180, 180);
        auto *s = dialog.number("Saturation", 0, -100, 100);
        auto *l = dialog.number("Lightness", 0, -100, 100);
        auto *c = dialog.check("Colorize", false);
        if (!dialog.run())
          return;
        params = {{"hue", h->value()},
                  {"saturation", s->value()},
                  {"lightness", l->value()},
                  {"colorize", bool(c->value())}};
      } else if (name == "levels") {
        auto *b = dialog.number("Input black", 0, 0, 253);
        auto *g = dialog.number("Midtone gamma", 1, .01, 9.99, .01);
        auto *w = dialog.number("Input white", 255, 2, 255);
        auto *ob = dialog.number("Output black", 0, 0, 255);
        auto *ow = dialog.number("Output white", 255, 0, 255);
        if (!dialog.run())
          return;
        params = {{"inBlack", b->value()},
                  {"gamma", g->value()},
                  {"inWhite", w->value()},
                  {"outBlack", ob->value()},
                  {"outWhite", ow->value()}};
      } else if (name == "curves") {
        int Y = dialog.area(280);
        auto *curve = new dome_photo::CurveEditor(310, Y, 256, 256);
        if (!dialog.run())
          return;
        params["points"] = Json::array();
        for (auto point : curve->points())
          params["points"].push_back(Json::array({point.first, point.second}));
      } else if (name == "exposure") {
        auto *e = dialog.number("Exposure (stops)", 0, -20, 20, .1);
        auto *o = dialog.number("Offset", 0, -.5, .5, .01);
        auto *g = dialog.number("Gamma correction", 1, .01, 9.99, .01);
        if (!dialog.run())
          return;
        params = {{"exposure", e->value()}, {"offset", o->value()}, {"gamma", g->value()}};
      } else if (name == "vibrance") {
        auto *v = dialog.number("Vibrance", 0, -100, 100);
        auto *s = dialog.number("Saturation", 0, -100, 100);
        if (!dialog.run())
          return;
        params = {{"vibrance", v->value()}, {"saturation", s->value()}};
      } else if (name == "posterize" || name == "threshold") {
        auto *n = dialog.number(name == "posterize" ? "Levels" : "Threshold level",
                                name == "posterize" ? 4 : 128, name == "posterize" ? 2 : 1, 255);
        if (!dialog.run())
          return;
        params = {{name == "posterize" ? "levels" : "level", n->value()}};
      } else if (name == "colorBalance") {
        std::vector<Fl_Value_Input *> values;
        for (auto tone : {"Shadows", "Midtones", "Highlights"})
          for (auto axis : {"C/R", "M/G", "Y/B"}) {
            std::string label = std::string(tone) + ": " + axis;
            auto *value = dialog.number("", 0, -100, 100);
            value->copy_label(label.c_str());
            values.push_back(value);
          }
        auto *preserve = dialog.check("Preserve luminosity", true);
        if (!dialog.run())
          return;
        int n = 0;
        for (auto tone : {"shadows", "midtones", "highlights"}) {
          params[tone] = Json::array();
          for (int i = 0; i < 3; ++i)
            params[tone].push_back(values[n++]->value());
        }
        params["preserveLuminosity"] = bool(preserve->value());
      } else if (name == "blackWhite") {
        std::vector<Fl_Value_Input *> values;
        int defaults[] = {40, 60, 40, 60, 20, 80};
        int n = 0;
        for (auto label : {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"})
          values.push_back(dialog.number(label, defaults[n++], -200, 300));
        if (!dialog.run())
          return;
        n = 0;
        for (auto key : {"reds", "yellows", "greens", "cyans", "blues", "magentas"})
          params[key] = values[n++]->value();
      } else if (name == "photoFilter") {
        auto *filter =
            dialog.choice("Filter", {"Warming (85)", "Warming (LBA)", "Warming (81)",
                                     "Cooling (80)", "Cooling (LBB)", "Cooling (82)", "Sepia"});
        auto *density = dialog.number("Density (%)", 25, 0, 100);
        auto *preserve = dialog.check("Preserve luminosity", true);
        if (!dialog.run())
          return;
        params = {{"filter", std::array<const char *, 7>{"warming85", "warmingLBA", "warming81",
                                                         "cooling80", "coolingLBB", "cooling82",
                                                         "sepia"}[filter->value()]},
                  {"density", density->value()},
                  {"preserveLuminosity", bool(preserve->value())}};
      } else
        return;
    }
  } else if (!native_control(id))
    return;
  for (auto &[key, value] : params.items())
    if (value.is_number_float()) {
      double number = value.get<double>();
      if (std::isfinite(number) && std::floor(number) == number && std::abs(number) < 9e15)
        value = static_cast<int64_t>(number);
    }
  execute(id, std::move(params));
}
std::string selected_path() const {
  int row = paths_->value() - 1;
  return row >= 0 && row < int(path_names_.size()) ? path_names_[row] : "work";
}
void brush_settings() {
  dome_photo::Dialog dialog("Brush Settings");
  auto *size = dialog.number("Diameter (pixels)", brush_size_->value(), .5, 5000, .5);
  auto *hardness = dialog.number("Hardness (%)", hardness_->value(), 0, 100);
  auto *opacity = dialog.number("Opacity (%)", brush_opacity_->value(), 1, 100);
  if (dialog.run()) {
    brush_size_->value(size->value());
    hardness_->value(hardness->value());
    brush_opacity_->value(opacity->value());
    options_bar_->redraw();
  }
}
