// SPDX-License-Identifier: MIT
#include <artcraft/commands.hpp>
#include <stdexcept>
#include <utility>

namespace artcraft {
void Commands::add(Command command) {
  if (command.id.empty() || command.menu_path.empty() || !command.execute)
    throw std::invalid_argument("A command needs an ID, menu path and handler");
  if (commands_.contains(command.id))
    throw std::invalid_argument("Duplicate command ID: " + command.id);
  for (const auto &[id, existing] : commands_)
    if (existing.menu_path == command.menu_path)
      throw std::invalid_argument("Duplicate command menu path: " + command.menu_path);
  auto id = command.id;
  commands_.emplace(id, std::move(command));
  order_.push_back(std::move(id));
}
CommandState Commands::state(const std::string &id) const {
  const auto found = commands_.find(id);
  if (found == commands_.end()) return {false, false};
  return found->second.state ? found->second.state() : CommandState{};
}
bool Commands::invoke(const std::string &id) const {
  const auto found = commands_.find(id);
  if (found == commands_.end() || !state(id).enabled) return false;
  found->second.execute();
  return true;
}
std::vector<const Command *> Commands::entries() const {
  std::vector<const Command *> result;
  for (const auto &id : order_) result.push_back(&commands_.at(id));
  return result;
}
} // namespace artcraft
