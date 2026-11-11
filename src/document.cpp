// SPDX-License-Identifier: MIT
#include <artcraft/document.hpp>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace artcraft {
namespace {
constexpr size_t maximum_file = 2 * 1024 * 1024;
constexpr size_t history_limit = 32;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void text(const Json &value, size_t limit) {
  require(value.is_string() && value.get_ref<const std::string &>().size() <= limit,
          "Invalid or oversized text field");
  require(value.get_ref<const std::string &>().find('\0') == std::string::npos,
          "Embedded NUL text is unsupported");
}
void integer(const Json &value, int low, int high) {
  require(value.is_number_integer() && value >= low && value <= high, "Integer outside document bounds");
}
std::string number_text(double value) {
  if (!std::isfinite(value)) throw std::runtime_error("#NUM!");
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(12) << value;
  return output.str();
}
std::optional<double> number(const std::string &value) {
  double parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(parsed))
    return parsed;
  return {};
}
struct Calculator;
struct Formula {
  Calculator &calculator;
  const std::string &source;
  size_t position = 1;
  int depth = 0;
  void space() { while (position < source.size() && source[position] == ' ') ++position; }
  bool eat(char c) { space(); if (position < source.size() && source[position] == c) { ++position; return true; } return false; }
  double expression();
  double term();
  double atom();
  std::pair<int, int> reference();
};
struct Calculator {
  const Json &cells;
  bool active[16][8]{};
  int budget = 10000;
  CellValue cell(int row, int column) {
    if (--budget < 0) throw std::runtime_error("#LIMIT!");
    if (row < 0 || row >= 16 || column < 0 || column >= 8) throw std::runtime_error("#REF!");
    if (active[row][column]) throw std::runtime_error("#CYCLE!");
    const auto &value = cells[row][column].get_ref<const std::string &>();
    if (value.empty()) return {"", 0.};
    if (value[0] != '=') return {value, number(value)};
    active[row][column] = true;
    try {
      Formula parser{*this, value};
      double result = parser.expression();
      parser.space();
      if (parser.position != value.size()) throw std::runtime_error("#PARSE!");
      auto display = number_text(result);
      active[row][column] = false;
      return {display, result};
    } catch (...) { active[row][column] = false; throw; }
  }
  double numeric(int row, int column) {
    auto result = cell(row, column);
    if (!result.number) throw std::runtime_error("#VALUE!");
    return *result.number;
  }
};
std::pair<int, int> Formula::reference() {
  space();
  if (position >= source.size() || source[position] < 'A' || source[position] > 'H')
    throw std::runtime_error("#REF!");
  int column = source[position++] - 'A';
  int row = 0, digits = 0;
  while (position < source.size() && source[position] >= '0' && source[position] <= '9') {
    row = row * 10 + source[position++] - '0';
    if (++digits > 2) throw std::runtime_error("#REF!");
  }
  if (!digits || row < 1 || row > 16) throw std::runtime_error("#REF!");
  return {row - 1, column};
}
double Formula::atom() {
  if (--calculator.budget < 0) throw std::runtime_error("#LIMIT!");
  if (++depth > 64) throw std::runtime_error("#DEPTH!");
  double result = 0;
  if (eat('+')) result = atom();
  else if (eat('-')) result = -atom();
  else if (eat('(')) {
    result = expression();
    if (!eat(')')) throw std::runtime_error("#PARSE!");
  } else if (source.compare(position, 4, "SUM(") == 0) {
    position += 4;
    auto [r1, c1] = reference();
    if (!eat(':')) throw std::runtime_error("#PARSE!");
    auto [r2, c2] = reference();
    if (!eat(')')) throw std::runtime_error("#PARSE!");
    for (int row = std::min(r1, r2); row <= std::max(r1, r2); ++row)
      for (int col = std::min(c1, c2); col <= std::max(c1, c2); ++col)
        result += calculator.numeric(row, col);
  } else {
    space();
    if (position < source.size() && source[position] >= 'A' && source[position] <= 'Z') {
      auto [row, column] = reference();
      result = calculator.numeric(row, column);
    } else {
      const char *first = source.data() + position;
      auto parsed = std::from_chars(first, source.data() + source.size(), result);
      if (parsed.ec != std::errc{} || parsed.ptr == first) throw std::runtime_error("#PARSE!");
      position = parsed.ptr - source.data();
    }
  }
  --depth;
  if (!std::isfinite(result)) throw std::runtime_error("#NUM!");
  return result;
}
double Formula::term() {
  double result = atom();
  for (;;) {
    if (eat('*')) result *= atom();
    else if (eat('/')) { auto divisor = atom(); if (divisor == 0) throw std::runtime_error("#DIV/0!"); result /= divisor; }
    else return result;
  }
}
double Formula::expression() {
  double result = term();
  for (;;) {
    if (eat('+')) result += term();
    else if (eat('-')) result -= term();
    else return result;
  }
}
std::string escaped(const std::string &value) {
  std::string result;
  for (char c : value) {
    if (c == '&') result += "&amp;";
    else if (c == '<') result += "&lt;";
    else if (c == '>') result += "&gt;";
    else if (c == '"') result += "&quot;";
    else result += c;
  }
  return result;
}
void remember(std::deque<Json> &history, Json value) {
  history.push_back(std::move(value));
  if (history.size() > history_limit) history.pop_front();
}
}
std::string kind_name(Kind kind) {
  switch (kind) {
    case Kind::word: return "wordcraft";
    case Kind::grid: return "gridcraft";
    case Kind::deck: return "deckcraft";
    case Kind::photo: return "photocraft";
    case Kind::vector: return "vectorcraft";
  }
  throw std::invalid_argument("Unknown ArtCraft document kind");
}
std::string extension(Kind kind) { return "." + kind_name(kind); }
Json blank_content(Kind kind) {
  switch (kind) {
    case Kind::word: return {{"text", ""}};
    case Kind::grid: return {{"cells", std::vector<std::vector<std::string>>(16, std::vector<std::string>(8))}};
    case Kind::deck: return {{"slides", Json::array({{{"title", "Untitled slide"}, {"body", ""}}})}};
    case Kind::photo: return {{"width", 64}, {"height", 48}, {"pixels", std::vector<int>(64 * 48 * 3, 255)}};
    case Kind::vector: return {{"shapes", Json::array()}};
  }
  throw std::invalid_argument("Unknown ArtCraft document kind");
}
void validate_content(Kind kind, const Json &content) {
  require(content.is_object(), "Document content must be an object");
  switch (kind) {
    case Kind::word: text(content.at("text"), 1024 * 1024); break;
    case Kind::grid: {
      const auto &cells = content.at("cells");
      require(cells.is_array() && cells.size() == 16, "Grid must have 16 rows");
      for (const auto &row : cells) {
        require(row.is_array() && row.size() == 8, "Grid must have 8 columns");
        for (const auto &cell : row) text(cell, 4096);
      }
      break;
    }
    case Kind::deck: {
      const auto &slides = content.at("slides");
      require(slides.is_array() && !slides.empty() && slides.size() <= 128, "Deck must have 1 to 128 slides");
      for (const auto &slide : slides) { text(slide.at("title"), 1024); text(slide.at("body"), 16384); }
      break;
    }
    case Kind::photo: {
      integer(content.at("width"), 1, 256); integer(content.at("height"), 1, 256);
      const auto &pixels = content.at("pixels");
      const size_t size = content.at("width").get<int>() * content.at("height").get<int>() * 3;
      require(pixels.is_array() && pixels.size() == size, "Raster pixel count does not match dimensions");
      for (const auto &pixel : pixels) integer(pixel, 0, 255);
      break;
    }
    case Kind::vector: {
      const auto &shapes = content.at("shapes");
      require(shapes.is_array() && shapes.size() <= 256, "Too many vector rectangles");
      for (const auto &shape : shapes) {
        integer(shape.at("x"), 0, 639); integer(shape.at("y"), 0, 399);
        integer(shape.at("width"), 1, 640); integer(shape.at("height"), 1, 400);
        require(shape.at("x").get<int>() + shape.at("width").get<int>() <= 640 &&
                shape.at("y").get<int>() + shape.at("height").get<int>() <= 400, "Rectangle exceeds canvas");
        const auto &color = shape.at("color");
        require(color.is_array() && color.size() == 3, "Invalid shape color");
        for (const auto &channel : color) integer(channel, 0, 255);
      }
      break;
    }
  }
}
CellValue evaluate_cell(const Json &grid, int row, int column) {
  try { return Calculator{grid.at("cells")}.cell(row, column); }
  catch (const std::runtime_error &error) { return {error.what(), {}}; }
}
void atomic_write(const std::filesystem::path &path, const std::string &bytes) {
  require(!path.filename().empty(), "A filename is required");
  require(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)), "Saving through a symlink is unsupported");
  auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
  std::string temporary = (parent / ("." + path.filename().string() + ".XXXXXX")).string();
  int descriptor = mkstemp(temporary.data());
  if (descriptor < 0) throw std::runtime_error("Cannot create save file: " + std::string(strerror(errno)));
  try {
    size_t offset = 0;
    while (offset < bytes.size()) {
      auto count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw std::runtime_error("Cannot write save file");
      offset += count;
    }
    if (fsync(descriptor) != 0) throw std::runtime_error("Cannot synchronize save file");
    const int closing = close(descriptor); descriptor = -1;
    if (closing != 0) throw std::runtime_error("Cannot close save file");
    if (rename(temporary.c_str(), path.c_str()) != 0) throw std::runtime_error("Cannot replace destination file");
    int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) throw std::runtime_error("Cannot open save directory for synchronization");
    const int synced = fsync(directory); close(directory);
    if (synced != 0) throw std::runtime_error("Cannot synchronize save directory");
  } catch (...) {
    if (descriptor >= 0) close(descriptor);
    unlink(temporary.c_str());
    throw;
  }
}
Document::Document(Kind kind) : kind_(kind), content_(blank_content(kind)), saved_(content_) {}
void Document::replace(Json content) {
  validate_content(kind_, content);
  if (content == content_) return;
  remember(undo_, content_); redo_.clear(); content_ = std::move(content);
}
void Document::edit(const std::function<void(Json &)> &operation) {
  auto candidate = content_; operation(candidate); replace(std::move(candidate));
}
void Document::undo() {
  if (undo_.empty()) return;
  remember(redo_, content_); content_ = std::move(undo_.back()); undo_.pop_back();
}
void Document::redo() {
  if (redo_.empty()) return;
  remember(undo_, content_); content_ = std::move(redo_.back()); redo_.pop_back();
}
void Document::reset() {
  content_ = blank_content(kind_); saved_ = content_; path_.clear(); undo_.clear(); redo_.clear();
}
void Document::load(const std::filesystem::path &path) {
  require(path.extension() == extension(kind_), "Use the application's native test project extension");
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot open document");
  std::string bytes(maximum_file + 1, '\0');
  input.read(bytes.data(), bytes.size()); bytes.resize(input.gcount());
  require(bytes.size() <= maximum_file && !input.bad(), "Document exceeds the 2 MiB limit or cannot be read");
  const auto envelope = Json::parse(bytes, [](int depth, Json::parse_event_t, Json &) {
    require(depth <= 32, "Project nesting exceeds the supported depth");
    return true;
  });
  require(envelope.at("format") == "artcraft-test" && envelope.at("version") == 1 &&
          envelope.at("application") == kind_name(kind_), "Unsupported project format, version or application");
  auto candidate = envelope.at("content");
  validate_content(kind_, candidate);
  content_ = std::move(candidate); saved_ = content_; path_ = path; undo_.clear(); redo_.clear();
}
void Document::save(const std::filesystem::path &path) {
  require(path.extension() == extension(kind_), "Save requires the application's native test project extension");
  validate_content(kind_, content_);
  auto bytes = Json{{"format", "artcraft-test"}, {"version", 1}, {"application", kind_name(kind_)},
                    {"content", content_}}.dump(2) + "\n";
  require(bytes.size() <= maximum_file, "Document exceeds the 2 MiB project limit");
  atomic_write(path, bytes); path_ = path; saved_ = content_;
}
std::string Document::export_extension() const {
  switch (kind_) {
    case Kind::word: return ".txt";
    case Kind::grid: return ".csv";
    case Kind::deck: return ".html";
    case Kind::photo: return ".ppm";
    case Kind::vector: return ".svg";
  }
  throw std::invalid_argument("Unknown export kind");
}
void Document::export_to(const std::filesystem::path &path) const {
  require(path.extension() == export_extension(), "Incorrect export filename extension");
  std::ostringstream output;
  output.imbue(std::locale::classic());
  switch (kind_) {
    case Kind::word: output << content_.at("text").get<std::string>(); break;
    case Kind::grid:
      for (int row = 0; row < 16; ++row) {
        for (int column = 0; column < 8; ++column) {
          if (column) output << ',';
          const auto value = evaluate_cell(content_, row, column).text;
          output << '"';
          for (char c : value) { if (c == '"') output << '"'; output << c; }
          output << '"';
        }
        output << '\n';
      }
      break;
    case Kind::deck:
      output << "<!doctype html><meta charset=\"utf-8\"><title>DeckCraft slides</title>\n";
      for (const auto &slide : content_.at("slides"))
        output << "<section><h1>" << escaped(slide.at("title")) << "</h1><pre>"
               << escaped(slide.at("body")) << "</pre></section>\n";
      break;
    case Kind::photo:
      output << "P6\n" << content_.at("width") << ' ' << content_.at("height") << "\n255\n";
      for (const auto &channel : content_.at("pixels")) output.put(static_cast<char>(channel.get<int>()));
      break;
    case Kind::vector:
      output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"400\" viewBox=\"0 0 640 400\">\n";
      for (const auto &shape : content_.at("shapes")) {
        const auto &color = shape.at("color");
        output << "<rect x=\"" << shape.at("x") << "\" y=\"" << shape.at("y")
               << "\" width=\"" << shape.at("width") << "\" height=\"" << shape.at("height")
               << "\" fill=\"rgb(" << color[0] << ',' << color[1] << ',' << color[2] << ")\"/>\n";
      }
      output << "</svg>\n"; break;
  }
  atomic_write(path, output.str());
}
} // namespace artcraft
