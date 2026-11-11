# DomeD office build

WordCraft, GridCraft and DeckCraft are the active applications, built over pinned
Storytold engines. PhotoCraft is a separately packaged image editor;
VectorCraft is excluded from builds, staging and associations. The ArtCraft AI application is not a dependency. SoundCraft,
DesignCraft and LightCraft remain reserved shared-base targets without binaries.
Historical sources and test artifacts are retained separately.

## Build and verification

Run from this directory. Requirements include Rust, Python, CMake, the patched
DomeD FLTK 1.4.5 toolkit, installed Unicode BDF fonts and Poppler's `pdftoppm`.
The native integration uses canonical sibling DomeD sources.

```sh
python3 scripts/upstream.py fetch
python3 scripts/upstream.py build
cmake -S . -B build-office -DCMAKE_BUILD_TYPE=Release \
  -DARTCRAFT_DOMED_SOURCE=/path/to/DomeD \
  -DCMAKE_PREFIX_PATH=/opt/fltk-vr/toolkit
cmake --build build-office -j4
ctest --test-dir build-office --output-on-failure
python3 tests/bitmap-ports.py
python3 tests/ports-native.py build-office
mkdir -p build-office/edit-verification
dbus-run-session -- build-office/dome-office-view-test upstream.lock.json \
  "$PWD/build-upstream/target/release" "$PWD/build-office/edit-verification"
dbus-run-session -- build-office/dome-office-form-test
python3 tests/ports-stage.py build-office office
python3 scripts/stage-domed-ports.py build-office/bitmap-stage --build build-office
```

Use an empty staging directory. Launch its `bin/wordcraft-domed`,
`bin/gridcraft-domed` or `bin/deckcraft-domed`, optionally with `--workspace DIR`
and a document filename. The wrapper delegates desktop/XR routing to `dome-link`.
The host uses the common native GPU window, universal menu and optional tray;
it preserves the selected compositor connection and starts no new compositor.
Legacy prototype editors are disabled by default.

Original upstream checkouts remain clean. SHA256-checked downstream patches are
applied in separate worktrees; `upstream.lock.json` also pins shared bitmap
sources and command catalogues. `scripts/upstream.py verify` checks provenance.

## Bitmap engine and output

The shared `bitmap/` provider reads installed Unicode BDF strikes directly,
including DWIDTH advances and BBX masks. HarfRust uses BDF-backed font callbacks
for shaping, with bidi and bitmap fallback. A minimal table directory adapts
HarfRust's shaping interface; it contains no outlines or converted glyphs.
Installed BDF families and styles are passed from canonical DomeD discovery.
Missing characters use another installed bitmap face or the BDF missing glyph.
No downloaded font assets or runtime outline fallback are introduced.

All three CLI engines require a valid BDF provider before creating sessions.
WordCraft and DeckCraft's font databases reject SFNT faces in this mode; GridCraft
uses BDF measurements and mask geometry. Status reports expose the selected font,
provider and `outlineFallback=false`. The host rejects older incompatible engines.
Original unused upstream font assets/code may still exist in the engine binary.

Native page images come from actual engine layout and rendering, including
rich text, tables, pictures, charts and slide objects. The host uploads these
images to GPU textures with nearest sampling and explicit viewport clipping.
PDF exports and FLTK print pages use bitmap-derived glyph geometry or raster
pages. PDF output currently has no searchable/selectable text layer; native
DOCX/XLSX/PPTX files retain their text and document models. Physical printer output
has not been certified.

## WordCraft window layout

WordCraft follows the macOS arrangement: the application, File, Edit, View,
Insert, Format, Tools, Table, Window and Help menus are exported through DomeD's
universal menu. File and Help are absent from the in-window ribbon. Home, Insert,
Draw, Design, Layout, References, Mailings, Review, View, Developer and Zotero
remain ribbon tabs, with contextual table/object tabs driven by the selection.
The native ribbon uses the pinned upstream's command groups, with installed BDF
family selection, size, emphasis, lists, alignment, styles and search controls.
Group launchers expose the remaining commands through native menus/forms.

The page editor centers continuous pages on a distinct workspace, with shadows,
optional navigation and horizontal/vertical rulers. Page width, one-page,
multiple-page and percentage zoom share one transform for drawing, selection and
pointer hit testing. Ruler handles change paragraph indents and vertical section
margins through upstream commands. Cached page rasters load around the visible
page and are bounded to eight images and 64 MiB of decoded pixels. Navigation
shows document headings and includes text search. The status bar shows page/word
counts and the actual display zoom. The paragraph replacement tool and command
workbench are optional View actions and do not occupy the default page workspace.
DomeD owns window decorations and the menu; no macOS title-bar decorations are
imitated. GridCraft and DeckCraft use the same menu/ribbon split. GridCraft has a compact
formula bar, sheet tabs and a bounded viewport with BDF cell fonts, basic styles,
colors, alignment, borders and workbook row/column dimensions. DeckCraft has a
centered slide stage and actual rendered thumbnails in its slide navigator.

`dome-word-ui-test LOCK ENGINE WORKSPACE` checks actual ribbon formatting and
state, continuous page geometry, mouse selection at different zooms, navigation
and layouts at 720×500 and 1100×760. `tests/ports-native.py` also verifies the
universal menu ordering and its navigation/zoom actions.

## Native editing and commands

* WordCraft supports engine-based caret positioning, drag/Shift/word/paragraph
  selection, selection highlights, typing, deletion, navigation and paste.
  Its page view uses actual layout, with page navigation and print/export.
* GridCraft provides a scrollable 1,048,576-row by 16,384-column table, bounded
  lazy data fetches, sheet navigation, range selection, address/formula editing,
  row/column resizing, calculation and undo/redo. Page preview uses the engine's
  print layout for faithful styles, charts and images. The interactive data view
  supports basic cell styling; advanced conditional formats and embedded-object
  editing retain limitations compared with the original bespoke frontend.
* DeckCraft renders full engine slides, supports engine pointer gestures and
  shape-text editing, and offers a native fullscreen player using upstream
  builds, animations, timed advances and transitions, including Morph.

The native universal menus bind all 1,387 pinned engine commands (671 Word,
385 Grid, 331 Deck), with authoritative enabled states. Common typed parameter
dialogs handle numbers, booleans, enums, files and text. Compound API parameters
still use structured JSON fields, and the Commands workbench remains available.
This is command coverage, not a complete recreation of every upstream bespoke
panel, presenter mode or media workflow.

The bottom editor replaces a paragraph/shape or sets a cell formula. Apply before
saving; unapplied text survives failures and blocks conflicting mutations. Edit >
Discard text edit cancels only that pending text. New/Open/Close guard unsaved
changes. Curated Open/Save operations stay inside the selected workspace; arbitrary
engine commands are not a security sandbox. Active engine document switches
synchronize the host's path and dirty state.

## Validation and deployment limits

Automated checks cover strict BDF parsing, shaping callbacks, malformed transport,
real rich-document rendering, no-outline PDF output, installed-family selection,
animation frames, Office save/reopen, native editing controls and last-cell Grid
access. Native menu tests exercise actual DBusMenu dispatch, GPU capture and Deck
fullscreen entry/exit. Native forms verify typed parameter serialization. Staging
tests verify checksums, relocatable launchers, dependencies and MIME-policy merges.

The prepared office associations replace Calligra for DOCX, ODT, XLSX and PPTX.
ODS/ODP remain unassigned because upstream round-trip support is not established.
Staging changes no live MIME defaults. These upstream engines are development
software; bitmap PDF searchability, specialized UI parity, real printer output
and physical XR interaction remain release qualification work. The build is not
marked as an activated production default suite.
