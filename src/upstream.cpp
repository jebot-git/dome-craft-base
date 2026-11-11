// SPDX-License-Identifier: MIT
#include <artcraft/upstream.hpp>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace artcraft {
using Json = nlohmann::json;
namespace {
constexpr size_t maximum_request = 256 * 1024;
constexpr size_t maximum_reply = 16 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
void check(bool valid, const char *message) { if (!valid) throw std::runtime_error(message); }
}
struct UpstreamProcess::Impl {
  int socket = -1;
  pid_t child = -1;
  uint64_t sequence = 0;
  bool failed = false;
  std::string pending;
  Impl(const std::string &executable, const std::vector<std::string> &arguments) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
      throw std::runtime_error("Cannot create private upstream transport");
    for (auto &descriptor : pair) {
      if (descriptor < 3) {
        int replacement = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) { close(pair[0]); close(pair[1]); throw std::runtime_error("Cannot reserve upstream descriptors"); }
        close(descriptor); descriptor = replacement;
      }
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pair[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pair[0]);
    posix_spawn_file_actions_addclose(&actions, pair[1]);
    std::vector<std::string> values{executable};
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    // PhotoCraft treats this inherited variable as a request to connect to a
    // different application. A DomeD-owned backend must always be headless.
    std::vector<std::string> environment;
    for (char **item = environ; *item; ++item)
      if (std::string(*item).rfind("PHOTOCRAFT_CONTROL_PORT=", 0) != 0)
        environment.emplace_back(*item);
    std::vector<char *> envp;
    for (auto &value : environment) envp.push_back(value.data());
    envp.push_back(nullptr);
    int error = posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (error) { close(pair[0]); throw std::runtime_error("Cannot start upstream engine: " + std::string(strerror(error))); }
    socket = pair[0];
    if (fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK) < 0) {
      close(socket); kill(child, SIGKILL); waitpid(child, nullptr, 0);
      throw std::runtime_error("Cannot configure upstream transport");
    }
  }
  ~Impl() {
    if (socket >= 0) { shutdown(socket, SHUT_RDWR); close(socket); }
    if (child > 0) {
      for (int attempt = 0; attempt < 20; ++attempt) {
        auto status = waitpid(child, nullptr, WNOHANG);
        if (status == child || (status < 0 && errno == ECHILD)) return;
        if (attempt == 0) kill(child, SIGTERM);
        poll(nullptr, 0, 10);
      }
      kill(child, SIGKILL);
      while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
    }
  }
  void wait(short events, Clock::time_point deadline) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (remaining <= 0) throw std::runtime_error("Upstream engine timed out; operation outcome is unknown");
    pollfd descriptor{socket, events, 0};
    int result;
    do { result = poll(&descriptor, 1, static_cast<int>(std::min<int64_t>(remaining, 1000))); } while (result < 0 && errno == EINTR);
    if (result < 0 || (descriptor.revents & POLLNVAL)) throw std::runtime_error("Upstream transport failed");
  }
  void send(const Json &value, Clock::time_point deadline) {
    check(!failed, "Upstream transport is unavailable after a previous failure");
    auto bytes = value.dump() + '\n';
    check(bytes.size() <= maximum_request, "Upstream request exceeds 256 KiB");
    size_t offset = 0;
    while (offset < bytes.size()) {
      auto count = ::send(socket, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
      if (count > 0) offset += count;
      else if (count < 0 && errno == EINTR) continue;
      else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) wait(POLLOUT, deadline);
      else throw std::runtime_error("Upstream engine closed its transport");
    }
  }
  Json read(Clock::time_point deadline) {
    for (;;) {
      if (auto newline = pending.find('\n'); newline != std::string::npos) {
        auto line = pending.substr(0, newline); pending.erase(0, newline + 1);
        return Json::parse(line, [](int depth, Json::parse_event_t, Json &) {
          check(depth <= 64, "Upstream response nesting exceeds 64 levels"); return true;
        });
      }
      char buffer[16384];
      auto count = recv(socket, buffer, sizeof buffer, 0);
      if (count > 0) {
        pending.append(buffer, count); check(pending.size() <= maximum_reply, "Upstream reply exceeds 16 MiB");
      } else if (count < 0 && errno == EINTR) continue;
      else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) wait(POLLIN, deadline);
      else throw std::runtime_error("Upstream engine exited before replying");
    }
  }
};
UpstreamProcess::UpstreamProcess(const std::string &executable, const std::vector<std::string> &arguments)
  : impl_(std::make_unique<Impl>(executable, arguments)) {}
UpstreamProcess::~UpstreamProcess() = default;
bool UpstreamProcess::available() const { return !impl_->failed; }
Json UpstreamProcess::request(const std::string &method, const Json &params, std::chrono::seconds timeout) {
  const auto id = ++impl_->sequence;
  const auto deadline = Clock::now() + timeout;
  try {
    impl_->send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}, deadline);
    for (int notification = 0; notification < 64; ++notification) {
      auto reply = impl_->read(deadline);
      if (!reply.contains("id")) continue;
      check(reply.at("id") == id, "Unexpected upstream response ID");
      if (reply.contains("error")) throw std::runtime_error(reply.at("error").dump());
      return reply.at("result");
    }
    throw std::runtime_error("Too many upstream notifications without a reply");
  } catch (...) { impl_->failed = true; throw; }
}
void UpstreamProcess::notify(const std::string &method, const Json &params) {
  try { impl_->send({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}}, Clock::now() + std::chrono::seconds(10)); }
  catch (...) { impl_->failed = true; throw; }
}
Json UpstreamProcess::tool(const std::string &name, const Json &arguments) {
  auto reply = request("tools/call", {{"name", name}, {"arguments", arguments}});
  if (reply.value("isError", false)) throw std::runtime_error(reply.dump());
  if (reply.contains("structuredContent")) return reply.at("structuredContent");
  for (const auto &item : reply.value("content", Json::array())) {
    if (item.value("type", "") == "text") {
      const auto value = item.value("text", "");
      auto parsed = Json::parse(value, [](int depth, Json::parse_event_t, Json &) {
        check(depth <= 64, "Upstream tool result nesting exceeds 64 levels"); return true;
      }, false);
      return parsed.is_discarded() ? Json{{"text", value}} : parsed;
    }
  }
  return reply;
}
} // namespace artcraft
