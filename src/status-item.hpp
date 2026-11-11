// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
namespace artcraft {
class StatusItem {
public:
  StatusItem(std::string id, std::string title, std::string menu_path,
             std::function<void()> activate);
  ~StatusItem();
  bool available() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace artcraft
