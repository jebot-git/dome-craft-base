// SPDX-License-Identifier: MIT
#pragma once
#include <FL/Fl_Group.H>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
class Fl_Hold_Browser;
class Fl_Input;
class Fl_Multiline_Input;
class Fl_Button;
class Fl_Scroll;
class Fl_Tabs;
namespace dome_office {
using Json = nlohmann::json;
using Submit = std::function<void(std::vector<std::pair<std::string, Json>>)>;
class Canvas;
class Grid;
class Input;
class OfficeUi;
class Ruler;
class SlideList;
// Views consume engine inspections; edits are commands on the original model.
class View : public Fl_Group {
  std::unique_ptr<OfficeUi> office_ui_;
  std::function<void(const std::string &)> invoke_;
  std::string project_;
  Json snapshot_ = Json::object();
  Submit submit_;
  Fl_Hold_Browser *navigator_;
  Fl_Input *address_;
  Fl_Multiline_Input *editor_;
  Fl_Button *apply_;
  Canvas *canvas_;
  Grid *grid_ = nullptr;
  SlideList *slide_list_ = nullptr;
  Fl_Scroll *scroll_;
  Fl_Tabs *sheet_tabs_ = nullptr;
  Fl_Button *add_sheet_ = nullptr;
  std::string sheet_names_;
  double zoom_ = 0.;
  Ruler *horizontal_ = nullptr, *vertical_ = nullptr;
  bool find_focus_ = false;
  int visible_page_ = 0;
  std::vector<int> navigation_blocks_;
  bool pending_ = false, busy_ = true, presentation_ = false;
  bool deck_editing_ = false, paragraph_editor_ = false;
  int block_ = -1, row_ = 0, col_ = 0;
  Json shape_id_;
  void navigate();
  void pick(int index);
  void apply();
  friend class Canvas;
  friend class Grid;
  friend class Input;
  friend class OfficeUi;
  friend class Ruler;
  friend class SlideList;
  void layout_editor();

public:
  View(int x, int y, int w, int h, std::string project, Submit submit,
       std::function<void(const std::string &)> invoke = {});
  ~View();
  void update(const Json &snapshot, bool busy);
  void presentation(bool value);
  void resize(int x, int y, int w, int h) override;
  void paragraph_editor(bool value);
  void message(const std::string &text);
  Json ui_state() const;
  void focus_document();
  void find();
  void paste();
  void commit() { apply(); }
  bool pending() const { return pending_; }
  void discard_pending() { pending_ = false; }
};
} // namespace dome_office
