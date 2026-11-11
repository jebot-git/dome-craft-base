// SPDX-License-Identifier: MIT
#include <artcraft/upstream_session.hpp>
#include <artcraft/raster.hpp>
#include <stdexcept>
#include <algorithm>
#include <cctype>
namespace artcraft {
using Json = nlohmann::json;
bool UpstreamSession::available() const { return process_ && process_->available(); }
UpstreamSession::UpstreamSession(Json configuration, const std::string &engine,
                                 const std::filesystem::path &workspace)
    : config_(std::move(configuration)), workspace_(std::filesystem::canonical(workspace)) {
  auto arguments = config_.at("mcp_args").get<std::vector<std::string>>();
  if (config_.value("capability_roots", false)) {
    arguments.insert(arguments.end(), {"--automation-read-root", workspace_.string(),
                                       "--automation-write-root", workspace_.string()});
  }
  process_ = std::make_unique<UpstreamProcess>(engine, arguments);
  process_->request("initialize",
                    {{"protocolVersion", "2024-11-05"},
                     {"capabilities", Json::object()},
                     {"clientInfo", {{"name", "DomeD native port"}, {"version", "0.1.0"}}}});
  process_->notify("notifications/initialized", Json::object());
  tools_ = process_->request("tools/list", Json::object()).at("tools");
  if (config_.at("name") == "photocraft")
    bitmap_ = {{"provider", "upstream-image-raster"}, {"native_ui", "domed-bdf"}};
  else
    bitmap_ = process_->tool("domed_bitmap_status", Json::object());
  if (config_.at("name") != "photocraft" &&
      (bitmap_.value("provider", "") != "domed-bdf-v1" || bitmap_.value("outlineFallback", true)))
    throw std::runtime_error("Engine does not provide the required bitmap-only font path");
  for (const auto &key : {"execute_tool", "commands_tool", "inspect_tool"}) {
    const auto name = config_.at(key).get<std::string>();
    bool found = false;
    for (const auto &tool : tools_)
      if (tool.value("name", "") == name)
        found = true;
    if (!found)
      throw std::runtime_error("Pinned engine is missing required tool: " + name);
  }
}
Json UpstreamSession::execute(const std::string &command, const Json &params) {
  if (!params.is_object())
    throw std::invalid_argument("Engine command parameters must be an object");
  return process_->tool(
      config_.at("execute_tool"),
      {{config_.value("command_key", std::string("command")), command}, {"params", params}});
}
Json UpstreamSession::tool(const std::string &name, const Json &params) {
  return process_->tool(name, params);
}
Json UpstreamSession::create() {
  if (config_.at("name") == "photocraft")
    return process_->tool("doc_new", {{"width", 1200}, {"height", 800}, {"name", "Untitled"}});
  return execute("file.new", Json::object());
}
void UpstreamSession::check_path(const std::filesystem::path &path) const {
  const auto candidate = std::filesystem::weakly_canonical(path);
  auto root = workspace_.begin(), child = candidate.begin();
  for (; root != workspace_.end(); ++root, ++child)
    if (child == candidate.end() || *child != *root)
      throw std::runtime_error("The first port limits files to its selected workspace directory");
}
Json UpstreamSession::open(const std::filesystem::path &path) {
  check_path(path);
  if (config_.at("name") == "photocraft")
    return process_->tool("doc_open", {{"path", path.string()}});
  return execute("file.open", {{"path", path.string()}});
}
Json UpstreamSession::save(const std::filesystem::path &path) {
  check_path(path);
  if (config_.at("name") == "photocraft")
    return process_->tool("doc_save", {{"path", path.string()}});
  return execute("file.save", {{"path", path.string()}});
}
Json UpstreamSession::snapshot() {
  auto document = process_->tool(config_.at("inspect_tool"), Json::object());
  Json view = Json::object();
  if (config_.at("name") == "wordcraft") {
    view["layout"] = execute("document.layout", Json::object());
    view["settings"] = execute("view.state", Json::object());
    view["format"] = execute("format.state", Json::object());
    auto focus = document.value("selection", Json::object()).value("focus", Json::object());
    if (focus.contains("path"))
      view["paragraph"] =
          execute("document.paragraph",
                  {{"path", focus["path"]}, {"story", focus.value("story", Json("body"))}});
  }
  if (config_.at("name") == "gridcraft") {
    auto column = [](int index) {
      std::string value;
      for (++index; index; index = (index - 1) / 26)
        value.insert(value.begin(), char('A' + (index - 1) % 26));
      return value;
    };
    auto address = [column](int row, int col) { return column(col) + std::to_string(row + 1); };
    const auto range =
        address(first_row_, first_column_) + ":" +
        address(std::min(first_row_ + 63, 1048575), std::min(first_column_ + 15, 16383));
    view["row"] = first_row_;
    view["column"] = first_column_;
    view["cells"] =
        execute("sheet.read", {{"range", range}, {"formatted", true}, {"styles", true}});
    view["inputs"] = execute("sheet.read", {{"range", range}, {"formulas", true}});
    view["active_input"] = execute("cell.get", Json::object());
    auto font = view["active_input"].value("style", Json::object()).value("font", Json::object());
    view["format"] = font;
    view["format"]["font"] = font.value("name", std::string());
    view["format"]["underline"] = font.value("underline", std::string("None")) != "None";
  } else if (config_.at("name") == "deckcraft") {
    view["slide"] = execute("slide.inspect", Json::object());
    view["format"] = execute("format.state", Json::object());
  }
  if (config_.at("name") == "photocraft")
    view["session"] = tool("session_list", Json::object());
  if (config_.at("name") == "wordcraft") {
    auto documents = execute("view.switchWindows", Json::object());
    for (const auto &entry : documents.value("documents", Json::array()))
      if (entry.value("active", false)) {
        document["path"] = entry.value("path", Json());
        document["dirty"] = entry.value("dirty", false);
        document["title"] = entry.value("title", std::string());
      }
  }
  auto commands = process_->tool(config_.at("commands_tool"), Json::object());
  if (commands.is_object() && commands.contains("commands"))
    commands = commands.at("commands");
  if (!commands.is_array() || commands.size() > 10000)
    throw std::runtime_error("Unexpected upstream command catalogue");
  for (auto &command : commands) {
    if (!command.contains("enabled") || !command.at("enabled").is_boolean())
      throw std::runtime_error("Engine catalogue is missing current command state");
    command["enabled_known"] = true;
  }
  return {{"application", config_.at("name")}, {"revision", config_.at("revision")},
          {"commands", std::move(commands)},   {"bitmap", bitmap_},
          {"document", std::move(document)},   {"view", std::move(view)}};
}
} // namespace artcraft

namespace artcraft {
nlohmann::json UpstreamSession::image_preview(const Json &params) {
  auto reply = process_->request("tools/call", {{"name", "render_preview"}, {"arguments", params}});
  if (reply.value("isError", false))
    throw std::runtime_error(reply.dump());
  for (const auto &item : reply.value("content", Json::array()))
    if (item.value("type", std::string()) == "image" &&
        item.value("mimeType", std::string()) == "image/png")
      return {{"png", item.at("data")}};
  throw std::runtime_error("Image engine returned no PNG preview");
}

nlohmann::json UpstreamSession::render(int page, bool explicit_page) {
  if (page < 0)
    throw std::runtime_error("Negative preview page");
  const auto name = config_.at("name").get<std::string>();
  if (name == "photocraft")
    return image_preview({{"max_side", 1600}});
  if (name == "deckcraft") {
    Json args = {{"scale", 4. / 3.}, {"edit", true}};
    if (explicit_page)
      args["slide"] = page;
    return execute("file.render", args);
  }
  if (name == "gridcraft") {
    auto pdf = execute("file.exportPdf", Json::object());
    const int pages = pdf.value("pages", 1);
    page = std::min(page, std::max(0, pages - 1));
    return {{"png", bitmap_pdf_png(pdf.at("base64"), page)}, {"page", page}, {"pages", pages}};
  }
  auto reply =
      process_->request("tools/call", {{"name", "render_page"},
                                       {"arguments", {{"page", page + 1}, {"scale", 4. / 3.}}}});
  if (reply.value("isError", false))
    throw std::runtime_error(reply.dump());
  Json result = Json::object();
  for (const auto &item : reply.value("content", Json::array())) {
    if (item.value("type", "") == "image" && item.value("mimeType", "") == "image/png")
      result["png"] = item.at("data");
    if (item.value("type", "") == "text") {
      auto info = Json::parse(item.value("text", ""), nullptr, false);
      if (info.is_object())
        result.update(info);
    }
  }
  if (!result.contains("png"))
    throw std::runtime_error("Engine preview returned no PNG");
  result["page"] = page;
  return result;
}
} // namespace artcraft

void artcraft::UpstreamSession::viewport(int row, int column) {
  if (row < 0 || row > 1048575 || column < 0 || column > 16383)
    throw std::runtime_error("Spreadsheet viewport is outside the sheet");
  first_row_ = row;
  first_column_ = column;
}

nlohmann::json artcraft::UpstreamSession::print_pages() {
  auto state = snapshot();
  auto doc = state.at("document");
  int count = doc.value("pages", 1);
  Json pdf;
  if (config_.at("name") == "deckcraft")
    count = doc.at("slides").size();
  if (config_.at("name") == "gridcraft") {
    pdf = execute("file.exportPdf", Json::object());
    count = pdf.at("pages");
  }
  if (count < 1 || count > 1000)
    throw std::runtime_error("Print job exceeds 1000 pages; use PDF export");
  Json pages = Json::array();
  size_t total = 0;
  for (int page = 0; page < count; ++page) {
    auto image =
        pdf.is_null() ? render(page, true) : Json{{"png", bitmap_pdf_png(pdf.at("base64"), page)}};
    total += image.at("png").get_ref<const std::string &>().size();
    if (total > 128 * 1024 * 1024)
      throw std::runtime_error("Print rasters exceed 128 MiB; use PDF export");
    pages.push_back(std::move(image));
  }
  return pages;
}

nlohmann::json artcraft::UpstreamSession::pointer(const nlohmann::json &params) {
  return process_->tool("pointer", params);
}
nlohmann::json artcraft::UpstreamSession::show(const std::string &action) {
  return process_->tool("domed_show", {{"action", action}});
}

nlohmann::json artcraft::UpstreamSession::select_text(const nlohmann::json &gesture) {
  auto before = execute("document.selection", Json::object());
  execute("caret.set", gesture.at("from"));
  auto start = execute("document.selection", Json::object()).at("focus");
  execute("caret.set", gesture.at("to"));
  auto focus = execute("document.selection", Json::object()).at("focus");
  if (gesture.value("extend", false))
    start = before.at("anchor");
  auto result = execute("select.range", {{"anchor", start}, {"focus", focus}});
  if (gesture.value("clicks", 0) > 0 && gesture.at("from") == gesture.at("to"))
    result = execute(gesture.value("clicks", 0) > 1 ? "select.paragraph" : "select.word",
                     Json::object());
  return result;
}
