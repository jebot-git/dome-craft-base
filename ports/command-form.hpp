// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace dome_office {
std::optional<nlohmann::json> command_form(const std::string &app,const nlohmann::json &command);
}
