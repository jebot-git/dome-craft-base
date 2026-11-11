# Native integration dependencies

ArtCraft uses the sibling DomeD repository's MIT-licensed menu exporter,
appearance, BDF discovery and GPU window code directly from its canonical
source path. These sources retain their SPDX notices; DomeD's MIT license is
in `../dome-compositor/LICENSE`. BDF enlargement also credits Francesco149/bdf2x
(Unlicense), with provenance in `../dome-compositor/native/bdf2x/`.

The native build links the installed patched FLTK toolkit, Fontconfig,
FreeType, zlib, GLib/GIO, Wayland, X11 compatibility support and graphics
libraries. Their licenses remain with their corresponding distributions.
No Calligra source or Qt/KDE engine has been imported. The separate upstream
port uses the MIT OR Apache-2.0 Storytold WordCraft, GridCraft, DeckCraft,
PhotoCraft and VectorCraft engines, pinned in `upstream.lock.json`. Their
original source/NOTICE/ATTRIBUTION files are retained. PhotoCraft has a recorded
TIFF build fix applied only in a separate build worktree; see `UPSTREAM-PORTS.md`.
No ArtCraft AI application, proprietary artwork or new font assets are included.
Shipping a standalone
binary distribution will require collecting the linked components' actual
license/source obligations and pinning the canonical integration revision.

## Office bitmap port

The shared BDF parser, HarfRust callback adapter and native presentation adapter
are MIT-licensed project sources under `bitmap/`. Installed BDF files remain
runtime system resources and are not redistributed in the package.

Only WordCraft, GridCraft and DeckCraft are staged. Their downstream patches
and source hashes are recorded in `upstream.lock.json`. GridCraft adds direct
HarfRust 0.13.3 and unicode-bidi 0.3.18 dependencies; HarfRust uses read-fonts
0.43.3. HarfRust uses MIT; read-fonts and unicode-bidi use MIT OR Apache-2.0.
Their license texts are retained in `notices/` and included in the stage. Poppler's `pdftoppm`
(GPL-licensed) is an external system executable used for bitmap spreadsheet page
previews, not a linked native GUI toolkit. Distribution must supply its notices
and source obligations through the system package.

The stage is a development bundle for an existing DomeD runtime, not a
self-contained distribution of FLTK, graphics drivers, Poppler or system fonts.

The PhotoCraft native dock requests bounded isolated layer previews through a
downstream extension of its headless preview API. The implementation retains
the upstream MIT OR Apache-2.0 licensing, alongside the pre-existing TIFF fix.
Native tool icons are original geometric drawing code. No Photoshop artwork,
brand marks, fonts or UI assets are redistributed by the native frontend.
