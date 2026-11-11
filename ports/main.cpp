// SPDX-License-Identifier: MIT
int dome_craft_port_main(const char *, const char *, const char *, int, char **);
int main(int argc, char **argv) {
  return dome_craft_port_main(DOME_CRAFT_PROJECT, ARTCRAFT_UPSTREAM_LOCK,
                              DOME_CRAFT_ENGINE_DIR, argc, argv);
}
