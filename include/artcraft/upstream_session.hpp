// SPDX-License-Identifier: MIT
#pragma once
#include <artcraft/upstream.hpp>
#include <filesystem>
namespace artcraft {
// Persistent upstream-owned document/session state. This adapter never converts
// an upstream file into the earlier test JSON model.
class UpstreamSession {
public:
  UpstreamSession(nlohmann::json configuration, const std::string &engine,
                  const std::filesystem::path &workspace);
  nlohmann::json execute(const std::string &command, const nlohmann::json &params);
  nlohmann::json create();
  nlohmann::json tool(const std::string &name, const nlohmann::json &params);
  nlohmann::json open(const std::filesystem::path &path);
  nlohmann::json save(const std::filesystem::path &path);
  nlohmann::json snapshot();
  nlohmann::json render(int page = 0, bool explicit_page = false);
  nlohmann::json print_pages();
  nlohmann::json image_preview(const nlohmann::json &params);
  nlohmann::json pointer(const nlohmann::json &params);
  nlohmann::json show(const std::string &action);
  nlohmann::json select_text(const nlohmann::json &gesture);
  void viewport(int row, int column);
  const nlohmann::json &bitmap_status() const { return bitmap_; }
  bool available() const;

private:
  nlohmann::json config_, tools_, bitmap_;
  std::filesystem::path workspace_;
  int first_row_ = 0, first_column_ = 0;
  std::unique_ptr<UpstreamProcess> process_;
  void check_path(const std::filesystem::path &path) const;
};
} // namespace artcraft
