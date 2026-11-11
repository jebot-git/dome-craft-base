// SPDX-License-Identifier: MIT
#include "editor.hpp"
int main(int argc, char **argv) {
  return artcraft::run_editor(static_cast<artcraft::Kind>(ARTCRAFT_PROJECT_INDEX), argc, argv);
}
