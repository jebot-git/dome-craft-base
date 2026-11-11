// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace artcraft {
struct CommandState {
  bool enabled = true;
  bool checked = false;
};
struct Command {
  std::string id;
  std::string menu_path;
  int shortcut = 0; // Native FLTK shortcut; zero for commands without one.
  bool checkable = false;
  std::function<void()> execute;
  std::function<CommandState()> state;
};

// UI-thread command model. No widget, window, bus or GPU dependency.
// State is queried again at invocation, so a stale enabled menu cannot execute
// a command whose document/selection state has subsequently changed.
class Commands {
public:
  void add(Command command);
  CommandState state(const std::string &id) const;
  bool invoke(const std::string &id) const;
  std::vector<const Command *> entries() const;
private:
  std::map<std::string, Command> commands_;
  std::vector<std::string> order_;
};
} // namespace artcraft
