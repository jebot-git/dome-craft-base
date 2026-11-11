// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace artcraft {
// Owns a headless upstream process over a private socket pair. No listening
// socket, shell interpolation, shared automation token or GUI frontend.
class UpstreamProcess {
public:
  UpstreamProcess(const std::string &executable, const std::vector<std::string> &arguments);
  ~UpstreamProcess();
  UpstreamProcess(const UpstreamProcess &) = delete;
  UpstreamProcess &operator=(const UpstreamProcess &) = delete;
  nlohmann::json request(const std::string &method, const nlohmann::json &parameters,
                        std::chrono::seconds timeout = std::chrono::seconds(120));
  void notify(const std::string &method, const nlohmann::json &parameters);
  nlohmann::json tool(const std::string &name, const nlohmann::json &arguments);
  bool available() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace artcraft
