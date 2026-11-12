// SPDX-License-Identifier: MIT
void popup(int x, int y, std::vector<dome_photo::MenuEntry> entries) {
  context_->open(x, y, std::move(entries), tool_variant_->textfont(), tool_variant_->textsize());
}
void tool_menu(const std::string &tool, int x, int y) {
  auto choices = variants(tool);
  std::vector<dome_photo::MenuEntry> entries;
  for (int i = 0; i < int(choices.size()); ++i)
    entries.push_back({choices[i],
                       [this, tool, i] {
                         tool_variants_[tool] = i;
                         set_tool(tool);
                       },
                       true, tool_ == tool && variant() == i});
  if (entries.empty())
    entries.push_back({"Select " + tool, [this, tool] { set_tool(tool); }});
  popup(x, y, std::move(entries));
}
void canvas_menu(int x, int y) {
  std::vector<dome_photo::MenuEntry> entries;
  bool selection = snapshot_.value("document", Json::object()).value("hasSelection", false);
  auto action = [&](const char *label, const char *id, bool enabled = true) {
    entries.push_back({label, [this, id] { control(id); }, enabled});
  };
  if (tool_ == "crop") {
    entries.push_back({"Crop", [this] { canvas_->apply_crop(); }, canvas_->crop_.is_array()});
    entries.push_back({"Cancel crop", [this] { canvas_->cancel_crop(); }});
  } else if (tool_ == "pen") {
    entries.push_back({"Finish open path", [this] { canvas_->finish_path(false); },
                       canvas_->points_.size() >= 2});
    entries.push_back(
        {"Close path", [this] { canvas_->finish_path(true); }, canvas_->points_.size() >= 3});
    entries.push_back({"Cancel path", [this] {
                         canvas_->points_ = Json::array();
                         canvas_->redraw();
                       }});
  } else if (tool_ == "path") {
    action("Make selection", "path.toSelection", !path_names_.empty());
    action("Stroke path...", "path.stroke", !path_names_.empty() && enabled_["path.stroke"]);
    action("Fill path", "path.fill", !path_names_.empty() && enabled_["path.fill"]);
    action("Transform path...", "path.transform", !path_names_.empty());
  } else if (tool_ == "brush" || tool_ == "eraser" || tool_ == "clone" || tool_ == "healing" ||
             tool_ == "blur" || tool_ == "dodge" || tool_ == "history")
    entries.push_back({"Brush settings...", [this] { brush_settings(); }});
  if (tool_ == "marquee" || tool_ == "lasso" || tool_ == "selection" || selection) {
    action("Deselect", "select.deselect", selection);
    action("Select inverse", "select.inverse", selection);
    action("Feather...", "select.modify.feather", selection);
    action("Expand...", "select.modify.expand", selection);
    action("Contract...", "select.modify.contract", selection);
    action("Transform selection...", "select.transformSelection", selection);
    action("Fill...", "edit.fill", selection);
    action("Stroke...", "edit.stroke", selection);
  }
  entries.push_back({"Undo", [this] { execute("edit.undo"); }, enabled_["edit.undo"]});
  entries.push_back({"Fit on screen", [this] {
                       zoom_ = 0;
                       canvas_->reset_pan();
                       canvas_->geometry();
                       canvas_->redraw();
                     }});
  entries.push_back({"100%", [this] { zoom(1); }});
  popup(x, y, std::move(entries));
}
void layer_menu(int row, int x, int y) {
  if (row < 1 || row > int(layer_ids_.size()))
    return;
  auto layer = layer_ids_[row - 1];
  layers_->value(row);
  if (busy_ || snapshot_.value("document", Json::object()).value("activeLayer", 0ul) != layer) {
    if (!busy_)
      execute("layer.select", {{"layer", layer}});
    after_idle_ = [this, row, x, y] { layer_menu(row, x, y); };
    return;
  }
  popup(x, y,
        {{"Layer properties...", [this] { control("layer.setProps"); }},
         {"Duplicate layer", [this, layer] { execute("layer.duplicate", {{"layer", layer}}); },
          enabled_["layer.duplicate"]},
         {"Delete layer", [this, layer] { execute("layer.delete", {{"layer", layer}}); },
          enabled_["layer.delete"]},
         {"Blending options...", [this] { control("layer.layerStyle.blendingOptions"); }},
         {"Add layer mask",
          [this, layer] { execute("layer.layerMask.revealAll", {{"layer", layer}}); }},
         {"New layer", [this] { execute("layer.new.layer"); }},
         {"New group", [this] { execute("layer.new.group"); }}});
}
void panel_menu(const std::string &panel, int row, int x, int y) {
  if (panel == "paths") {
    paths_->value(row);
    popup(x, y,
          {{"Make selection", [this] { control("path.toSelection"); }},
           {"Stroke path...", [this] { control("path.stroke"); }},
           {"Fill path", [this] { control("path.fill"); }},
           {"Transform path...", [this] { control("path.transform"); }},
           {"Delete path", [this] { execute("path.delete", {{"name", selected_path()}}); }}});
  } else if (panel == "channels") {
    channels_->value(row);
    popup(x, y,
          {{"New channel...", [this] { control("channel.new"); }},
           {"Delete channel", [this] { control("channel.delete"); },
            row > 1 + int(snapshot_.value("document", Json::object())
                              .value("channels", Json::object())
                              .value("colors", Json::array())
                              .size()) &&
                row <= int(channel_names_.size())}});
  } else if (panel == "history") {
    popup(x, y,
          {{"Undo", [this] { execute("edit.undo"); }, enabled_["edit.undo"]},
           {"Redo", [this] { execute("edit.redo"); }, enabled_["edit.redo"]},
           {"Set history brush source",
            [this, row] { execute("history.setSource", {{"state", std::max(0, row - 1)}}); },
            row > 0}});
  }
}
void begin_type(double x, double y) {
  type_x_ = x;
  type_y_ = y;
  type_editor_->value("");
  auto paper = canvas_->paper();
  int left = std::clamp(paper.x + int(x * canvas_->scale()), canvas_->x(),
                        canvas_->x() + std::max(0, canvas_->w() - 240));
  int top = std::clamp(paper.y + int(y * canvas_->scale()), canvas_->y(),
                       canvas_->y() + std::max(0, canvas_->h() - 120));
  type_editor_->resize(left, top, std::min(400, canvas_->w()), 120);
  type_editor_->show();
  type_editor_->take_focus();
  type_editor_->redraw();
}
void finish_type(bool accept) {
  if (accept && type_editor_->visible() && *type_editor_->value())
    execute("type.create", {{"x", type_x_},
                            {"y", type_y_},
                            {"text", type_editor_->value()},
                            {"size", type_size_->value()},
                            {"color", color()},
                            {"align", std::array<const char *, 3>{"left", "center",
                                                                  "right"}[type_align_->value()]}});
  type_editor_->hide();
  canvas_->take_focus();
  canvas_->redraw();
}
