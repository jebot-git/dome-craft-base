// SPDX-License-Identifier: MIT
#include <artcraft/upstream_session.hpp>
#include <fstream>
#include <iostream>
using Json = nlohmann::json;
int main(int argc, char **argv) {
  if (argc != 6) { std::cerr << "probe LOCK PROJECT ENGINE WORKSPACE SAVE_PATH\n"; return 2; }
  try {
    Json lock; std::ifstream input(argv[1]); input >> lock;
    Json config;
    for (const auto &row : lock.at("projects")) if (row.at("name") == argv[2]) config = row;
    if (config.is_null()) throw std::runtime_error("Unknown project");
    Json output;
    {
      artcraft::UpstreamSession session(config, argv[3], argv[4]);
      output["created"] = session.create();
      output["before"] = session.snapshot();
      output["edit"] = session.execute(config.at("smoke_command"), config.at("smoke_params"));
      output["after"] = session.snapshot();
      output["undo"] = session.execute("edit.undo", Json::object());
      output["redo"] = session.execute("edit.redo", Json::object());
      output["save"] = session.save(argv[5]);
    }
    artcraft::UpstreamSession reopened(config, argv[3], argv[4]);
    reopened.open(argv[5]); output["reopened"] = reopened.snapshot();
    std::cout << output.dump(2) << '\n';
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
