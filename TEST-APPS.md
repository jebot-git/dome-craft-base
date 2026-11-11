> Historical prototype guide. These fixtures require `DOME_OFFICE_BUILD_LEGACY_FIXTURES=ON`. Current upstream ports are described in [OFFICE-PORT.md](OFFICE-PORT.md) and [PHOTO-PORT.md](PHOTO-PORT.md).

# ArtCraft test applications

These five small native editors exercise the shared DomeD base with real models,
undo/redo, file operations and GPU-backed windows. They are test implementations,
not full office/graphics replacements. They do not read ODF, OOXML, PSD or
arbitrary SVG/raster files.

| Package | Editing supported | Native project | Export |
| --- | --- | --- | --- |
| WordCraft | UTF-8 plain text with wrapping and paragraph insertion | `.wordcraft` | UTF-8 `.txt` |
| GridCraft | One 16-row, 8-column sheet; cell input; formulas and range sum | `.gridcraft` | Evaluated `.csv` |
| DeckCraft | Slide titles/body text, add/delete, navigation and preview | `.deckcraft` | Static `.html` outline |
| PhotoCraft | RGB pixel painting with three brush colors, invert and clear | `.photocraft` | Binary RGB `.ppm` |
| VectorCraft | Add/select/drag/delete filled rectangles and change fill | `.vectorcraft` | Rectangle-only `.svg` |

DeckCraft navigates slides in order; it does not yet reorder them. Its presentation
preview stays in the editor window. There is no separate audience view. PhotoCraft
starts with a 64 x 48 canvas and accepts native projects up to 256 x 256 pixels.
VectorCraft uses a 640 x 400 logical canvas. No layers, rich text, pagination,
complex paths, ICC workflow, animation or compatibility fidelity is implied.

## Build, run and package

Use the base build prerequisites from `README.md`, including nlohmann-json:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/opt/fltk-vr/toolkit
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python3 scripts/package-tests.py --build build --output build/packages
```

Each archive has its own binary, a realm-aware launcher, desktop entry, license
notices, this guide and a SHA-256 manifest. Extract to a user-owned directory and
run `bin/wordcraft` (or the other project name). The launcher locates its adjacent
native binary and delegates routing to installed `dome-link` when available.
The packages require this host's compatible DomeD/FLTK/BDF runtime; they are not
self-contained cross-distribution packages. No installer or MIME-default change
is performed. The shared base is linked into each executable for these tests.

Direct development binaries are `build/wordcraft-native`, `build/gridcraft-native`,
`build/deckcraft-native`, `build/photocraft-native` and `build/vectorcraft-native`.
Supply a project path to open it, or a new path with the matching extension as
the first save destination. Without a path, Save uses the native file chooser.

All apps expose New, Open, Save, Save As, Export, Undo, Redo and Close through
DomeD's universal menu and standard Ctrl shortcuts. Closing or replacing a dirty
document offers Cancel, Discard and Save. `--tray` opts into the shared tray
lifecycle; closing still follows the document policy. Outside DomeD the local
menu remains visible.

WordCraft edits its text directly. GridCraft selects cells in the canvas and
edits their source in the input above it. Its formula subset is uppercase A1:H16
references, finite numbers, unary signs, parentheses, `+ - * /` and
`SUM(A1:B2)`-style rectangular ranges. Errors include parse, reference, text-value,
cycle, divide-by-zero and calculation-budget errors. It has no relative-copy,
cross-sheet, date, currency or locale semantics. CSV exports evaluated display
values and preserves text with CSV quoting.

DeckCraft edits title/body fields and selects slides in the list. Use Slide >
Add slide or Ctrl+M to add one. PhotoCraft paints while dragging on the canvas;
each completed stroke is one undo step. Brush colors and Image operations are
in the universal menu. VectorCraft adds rectangles through Object > Add rectangle
or Ctrl+R; click to select and drag to move, with one undo step per gesture.

## Files and limitations

The five native project formats are explicit `artcraft-test` JSON envelopes,
version 1, with application identity and validated model content. Limits include
2 MiB input/output project size, 32 JSON nesting levels, at most 128 slides or
256 rectangles and 32 undo snapshots. Save validates the model, writes a sibling
temporary file, fsyncs it, replaces the destination and fsyncs the directory.
Native saves and exports require the appropriate suffix; saving through a
symlink is rejected. A failed load leaves the current document intact.

These prototypes do not implement automatic recovery journals or a complete
external-edit conflict workflow. Keep independent copies of important files.
Native projects carry only the implemented content; exported text/CSV/HTML/PPM/SVG
cannot be reopened as full editable projects. Native files are test formats
whose future migration policy is not established.

PhotoCraft retains CPU-authoritative pixels and a GL texture, uploading only
after a canvas revision or GL context recreation. Rendering remains within the
shared FLTK GPU path. File parsing, formula evaluation, glyph rasterization,
painting operations and exports use the CPU. Neither these packages nor the base
claim every operation or compositor transport is GPU-only.

## Tests

```sh
ctest --test-dir build --output-on-failure
/usr/bin/python3 tests/editors-native.py build > build/editors-validation.json
python3 scripts/package-tests.py --build build --output build/packages
python3 tests/packages.py build/packages > build/packages-validation.json
```

Native tests require Python Gio/GLib bindings and a running Wayland display. They
open each editor temporarily on the caller's display and use a private D-Bus
session. The checks exercise each application's actual menu action, undo/redo,
save, independently parsed export and fresh-process reopen. GL framebuffer
captures verify PhotoCraft's pixel colors/orientation and VectorCraft's rendered
rectangle. Captures are saved under `build/editor-captures/`. Physical XR and
exhaustive pointer/IME/accessibility behavior are not certified by these tests.

Package checks extract all five archives, verify every manifest hash and desktop
entry, inspect native linkage and execute the relocated launchers while checking
exact Unicode/space/metacharacter argument forwarding.

Optional `--report FILE`, `--capture FILE.ppm`, `--export FILE` and
`--quit-after-ms N` are QA hooks.
The report contains private test model data; it is only written when requested.
`--export` supplies the Export command's destination; it does not export on
startup. Timed close uses the normal unsaved-document policy.
Capture explicitly reads the completed GL framebuffer before buffer swap; this
readback is confined to requested QA and is not part of normal rendering.
