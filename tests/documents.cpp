// SPDX-License-Identifier: MIT
#include <artcraft/document.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace artcraft;
namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void rejects(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::exception &) { rejected = true; }
  check(rejected, "Invalid document operation accepted");
}
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main() {
  char pattern[] = "/tmp/artcraft-documents-XXXXXX";
  const char *created = mkdtemp(pattern);
  if (!created) return 1;
  std::filesystem::path directory(created);
  try {
    for (auto kind : {Kind::word, Kind::grid, Kind::deck, Kind::photo, Kind::vector}) {
      Document doc(kind);
      const auto original = doc.content();
      const auto path = directory / ("test" + extension(kind));
      switch (kind) {
        case Kind::word: doc.edit([](Json &j) { j["text"] = "Native text — 日本語\nSecond paragraph.\n"; }); break;
        case Kind::grid: doc.edit([](Json &j) { j["cells"][0][0] = "2"; j["cells"][0][1] = "3"; j["cells"][0][2] = "=A1+B1*2"; }); break;
        case Kind::deck: doc.edit([](Json &j) { j["slides"].push_back({{"title", "<Second> & slide"}, {"body", "日本語"}}); }); break;
        case Kind::photo: doc.edit([](Json &j) { j["pixels"][0] = 10; }); break;
        case Kind::vector: doc.edit([](Json &j) { j["shapes"].push_back({{"x", 20}, {"y", 30}, {"width", 40}, {"height", 50}, {"color", {1, 2, 3}}}); }); break;
      }
      const auto edited = doc.content();
      check(doc.dirty() && doc.can_undo(), "Model edit did not become dirty");
      doc.undo(); check(doc.content() == original && !doc.dirty(), "Undo did not restore model");
      doc.redo(); check(doc.content() == edited, "Redo did not restore edit");
      doc.save(path); check(!doc.dirty(), "Save did not clear dirty state");
      Document loaded(kind); loaded.load(path);
      check(loaded.content() == edited && !loaded.dirty(), "Native save/reopen changed data");
      doc.undo(); check(doc.dirty(), "Undo across saved state did not become dirty");
      doc.redo(); check(!doc.dirty(), "Redo to saved state remained dirty");
      const auto output = directory / (kind_name(kind) + loaded.export_extension());
      loaded.export_to(output); check(!read(output).empty(), "Export empty");
      if (kind == Kind::deck) check(read(output).find("&lt;Second&gt; &amp; slide") != std::string::npos, "HTML export escaped incorrectly");
      if (kind == Kind::photo) check(read(output).size() == 13 + 64 * 48 * 3, "PPM export has incorrect dimensions or payload");
      const auto saved_bytes = read(path);
      rejects([&] { doc.export_to(path); });
      check(read(path) == saved_bytes, "Export rejection damaged source");
      rejects([&] { loaded.save(directory / "wrong.odt"); });
      auto wrong = Json::parse(saved_bytes); wrong["version"] = 99;
      atomic_write(path, wrong.dump());
      rejects([&] { loaded.load(path); });
      check(loaded.content() == edited, "Failed load damaged current document");
      doc.save(path);
    }
    auto grid = blank_content(Kind::grid);
    grid["cells"][0][0] = "2"; grid["cells"][0][1] = "3";
    grid["cells"][0][2] = "=A1+B1*2";
    check(evaluate_cell(grid, 0, 2).text == "8", "Arithmetic precedence/reference failed");
    grid["cells"][0][2] = "=SUM(A1:B1)";
    check(evaluate_cell(grid, 0, 2).text == "5", "SUM range failed");
    grid["cells"][0][2] = "=-(A1+B1)/2";
    check(evaluate_cell(grid, 0, 2).text == "-2.5", "Parentheses or unary minus failed");
    grid["cells"][0][2] = "=A1/0";
    check(evaluate_cell(grid, 0, 2).text == "#DIV/0!", "Division by zero not reported");
    grid["cells"][0][2] = "=I1";
    check(evaluate_cell(grid, 0, 2).text == "#REF!", "Invalid reference not reported");
    grid["cells"][0][0] = "=B1"; grid["cells"][0][1] = "=A1";
    check(evaluate_cell(grid, 0, 0).text == "#CYCLE!", "Cycle not reported");
    grid["cells"][0][0] = "text";
    check(evaluate_cell(grid, 0, 1).text == "#VALUE!", "Text coercion silently changed values");
    grid["cells"][0][0] = "=2+";
    check(evaluate_cell(grid, 0, 0).text == "#PARSE!", "Malformed formula accepted");
    // Expanding repeated references must terminate even without a cycle.
    grid = blank_content(Kind::grid);
    for (int index = 0; index < 30; ++index) {
      const int next = index + 1;
      const auto reference = std::string(1, 'A' + next % 8) + std::to_string(next / 8 + 1);
      grid["cells"][index / 8][index % 8] = "=" + reference + "+" + reference;
    }
    grid["cells"][3][6] = "1";
    check(evaluate_cell(grid, 0, 0).text == "#LIMIT!", "Unbounded repeated-reference calculation");
    auto raster = blank_content(Kind::photo); raster["pixels"].erase(0);
    rejects([&] { validate_content(Kind::photo, raster); });
    auto vector = blank_content(Kind::vector);
    vector["shapes"].push_back({{"x", 630}, {"y", 0}, {"width", 40}, {"height", 10}, {"color", {1, 2, 3}}});
    rejects([&] { validate_content(Kind::vector, vector); });
    auto invalid_word = blank_content(Kind::word); invalid_word["text"] = std::string("a\0b", 3);
    rejects([&] { validate_content(Kind::word, invalid_word); });
    const auto destination = directory / "target";
    atomic_write(destination, "preserved");
    const auto link = directory / "link";
    std::filesystem::create_symlink(destination, link);
    rejects([&] { atomic_write(link, "changed"); });
    check(read(destination) == "preserved", "Save wrote through symlink");
    const auto deep_file = directory / "deep.wordcraft";
    atomic_write(deep_file, std::string(64, '[') + "0" + std::string(64, ']'));
    Document depth_test(Kind::word);
    rejects([&] { depth_test.load(deep_file); });
    std::filesystem::remove(deep_file);
    check(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 12,
          "Unexpected temporary files left after saves");
    std::cout << "Five model/save/export round trips, undo/redo, formulas and invalid-file checks passed\n";
    std::filesystem::remove_all(directory);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; std::filesystem::remove_all(directory); return 1;
  }
}
