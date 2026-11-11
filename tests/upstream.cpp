// SPDX-License-Identifier: MIT
#include <artcraft/upstream.hpp>
#include <iostream>
#include <stdexcept>
using Json = nlohmann::json;
void check(bool valid, const char *message) { if (!valid) throw std::runtime_error(message); }
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  try {
    {
      artcraft::UpstreamProcess engine(argv[1], {argv[2]});
      engine.notify("notifications/initialized", Json::object());
      const Json input{{"text", "日本語 spaces ; $(not-a-shell)"}};
      check(engine.request("echo", input) == input, "Transport changed request arguments");
      check(engine.request("notifications", input) == input, "Notification confused request matching");
      check(engine.tool("echo", input).at("received") == input, "Tool result decode failed");
      bool rejected = false;
      try { engine.tool("reject", Json::object()); } catch (const std::exception &) { rejected = true; }
      check(rejected && engine.available(), "Command error poisoned a live transport");
      check(engine.request("echo", input) == input, "Command failure lost subsequent requests");
    }
    for (const auto *failure : {"wrong-id", "malformed", "huge", "slow"}) {
      artcraft::UpstreamProcess engine(argv[1], {argv[2]});
      bool rejected = false;
      try { engine.request(failure, Json::object(), std::chrono::seconds(1)); }
      catch (const std::exception &) { rejected = true; }
      check(rejected && !engine.available(), "Bad transport result was accepted");
    }
    std::cout << "Private RPC round trips, notifications, command failures, bounds, IDs and deadlines passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
