// SPDX-License-Identifier: MIT
#include <artcraft/commands.hpp>
#include <iostream>
#include <stdexcept>
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument &) { rejected = true; }
  require(rejected, "Invalid command accepted");
}
int main() {
  try {
    artcraft::Commands commands;
    bool selection = false, checked = false;
    int edits = 0;
    commands.add({"apply", "Edit/Apply", 0, false, [&] { ++edits; },
      [&] { return artcraft::CommandState{selection, false}; }});
    commands.add({"grid", "View/Grid", 0, true, [&] { checked = !checked; },
      [&] { return artcraft::CommandState{true, checked}; }});
    require(!commands.invoke("apply") && edits == 0, "Disabled command executed");
    selection = true;
    require(commands.invoke("apply") && edits == 1, "Selection command did not execute");
    // A previously enabled menu does not bypass current document state.
    require(commands.state("apply").enabled, "Selection state lost");
    selection = false;
    require(!commands.invoke("apply") && edits == 1, "Stale enabled state executed");
    require(commands.invoke("grid") && commands.state("grid").checked, "Toggle lost state");
    require(commands.invoke("grid") && !commands.state("grid").checked, "Toggle stuck");
    require(!commands.invoke("missing") && !commands.state("missing").enabled,
            "Unknown command enabled");
    rejects([&] { commands.add({"apply", "File/Other", 0, false, [] {}, {}}); });
    rejects([&] { commands.add({"other", "Edit/Apply", 0, false, [] {}, {}}); });
    rejects([&] { commands.add({"", "Bad", 0, false, [] {}, {}}); });
    rejects([&] { commands.add({"empty-handler", "Bad", 0, false, {}, {}}); });
    require(commands.entries().size() == 2 && commands.entries()[0]->id == "apply",
            "Registration order changed");
    std::cout << "Command state, disabled dispatch, toggles and validation passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
