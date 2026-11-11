// SPDX-License-Identifier: MIT
#pragma once
#include "office-view.hpp"
#include <memory>
namespace dome_office {
// Native controls organized using the pinned upstream ribbon's tabs and locations.
class OfficeUi {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  OfficeUi(View &view);
  ~OfficeUi();
  void update(const Json &snapshot, bool busy);
  void layout();
  void message(const std::string &text);
  void geometry(int page, double scale);
  Json state() const;
  void visible(bool value);
  int top() const;
  int bottom() const;
};
} // namespace dome_office
