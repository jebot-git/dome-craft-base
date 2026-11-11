// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace artcraft {
using Json = nlohmann::json;
enum class Kind { word, grid, deck, photo, vector };
std::string kind_name(Kind kind);
std::string extension(Kind kind);
Json blank_content(Kind kind);
void validate_content(Kind kind, const Json &content);
// Same-directory temporary file, fsync and rename. Never writes through a
// symlink and never truncates the destination before the replacement is ready.
void atomic_write(const std::filesystem::path &path, const std::string &bytes);

struct CellValue {
  std::string text;
  std::optional<double> number;
};
CellValue evaluate_cell(const Json &grid, int row, int column);

class Document {
public:
  explicit Document(Kind kind);
  Kind kind() const { return kind_; }
  const Json &content() const { return content_; }
  const std::filesystem::path &path() const { return path_; }
  bool dirty() const { return content_ != saved_; }
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  void replace(Json content);
  void edit(const std::function<void(Json &)> &operation);
  void undo();
  void redo();
  void reset();
  void load(const std::filesystem::path &path);
  void save(const std::filesystem::path &path);
  std::string export_extension() const;
  void export_to(const std::filesystem::path &path) const;
private:
  Kind kind_;
  Json content_, saved_;
  std::filesystem::path path_;
  std::deque<Json> undo_, redo_;
};
} // namespace artcraft
