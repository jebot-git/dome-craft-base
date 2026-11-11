// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <string_view>
namespace artcraft {
struct ProjectIdentity {
  std::string_view name;
  std::string_view executable;
  std::string_view app_id;
};
// Stable identities shared by the test editors and future full applications.
inline constexpr std::array<ProjectIdentity, 5> projects{{
  {"WordCraft", "wordcraft", "org.domed.ArtCraft.WordCraft"},
  {"GridCraft", "gridcraft", "org.domed.ArtCraft.GridCraft"},
  {"DeckCraft", "deckcraft", "org.domed.ArtCraft.DeckCraft"},
  {"PhotoCraft", "photocraft", "org.domed.ArtCraft.PhotoCraft"},
  {"VectorCraft", "vectorcraft", "org.domed.ArtCraft.VectorCraft"},
}};
inline constexpr std::array<ProjectIdentity, 3> domed_projects{{
  {"WordCraft", "wordcraft-domed", "org.domed.WordCraft"},
  {"GridCraft", "gridcraft-domed", "org.domed.GridCraft"},
  {"DeckCraft", "deckcraft-domed", "org.domed.DeckCraft"},
}};
// Extension points only: no binaries, source fetches or file associations yet.
inline constexpr std::array<ProjectIdentity, 3> future_projects{{
  {"SoundCraft", "soundcraft", "org.domed.SoundCraft"},
  {"DesignCraft", "designcraft", "org.domed.DesignCraft"},
  {"LightCraft", "lightcraft", "org.domed.LightCraft"},
}};
} // namespace artcraft
