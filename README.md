# DomeD native office base

The office suite is **WordCraft, GridCraft and DeckCraft**; **PhotoCraft** is a
separately packaged image editor. VectorCraft is excluded. SoundCraft, DesignCraft
and LightCraft remain future targets. The ArtCraft AI application is not included.

The real pinned Storytold engines own the documents and editing operations. The
shared DomeD FLTK frontend supplies native GPU windows, bitmap UI fonts,
universal menus and desktop/XR routing. Office engines use direct BDF metrics
and masks for shaping, layout, rendering and PDF output.

All three office windows use a macOS-style universal-menu/ribbon split. WordCraft
provides centered continuous pages, rulers, navigation and zoom; GridCraft uses a
formula bar, full-sheet viewport and sheet tabs; DeckCraft uses a fitted slide
stage, rendered slide thumbnails and a native presentation player. PhotoCraft
uses the macOS Photoshop workspace arrangement with native tools and docks.

See [the office implementation](OFFICE-PORT.md) and [PhotoCraft implementation](PHOTO-PORT.md)
for builds, validation and concrete limits. These are development ports. Bitmap
PDF text is not searchable, and advanced bespoke-panel parity and physical XR
validation remain qualification work.

## Application forks

Each application has its own downstream `domed` branch. Its `native/domed`
entry point builds that fork's engine and fetches this shared base at an
immutable commit recorded in `base.lock.json`.

| Application | Downstream fork |
| --- | --- |
| WordCraft | [wordcraft-xr](https://github.com/jebot-git/wordcraft-xr/tree/domed) |
| GridCraft | [gridcraft-xr](https://github.com/jebot-git/gridcraft-xr/tree/domed) |
| DeckCraft | [deckcraft-xr](https://github.com/jebot-git/deckcraft-xr/tree/domed) |
| PhotoCraft | [photocraft-xr](https://github.com/jebot-git/photocraft-xr/tree/domed) |

```sh
python3 native/domed/build.py --domed-source /path/to/DomeD \
  --toolkit /opt/fltk-vr/toolkit
```

Run this command inside an application fork. The office suite policy lives in
`packaging/craft-suite.json`; staging generates proposed Calligra replacements
without changing the live desktop's MIME defaults.

The sections below describe the original shared base and historical test fixtures.
Legacy editor fixtures are now disabled by default.

## Implemented

- `ArtCraft::Commands`: ordered command registration, duplicate validation,
  current enabled/checked state and guarded dispatch without FLTK dependencies.
  Buttons, shortcuts and exported menu actions invoke the same command handlers.
- `ArtCraft::Platform`: native FLTK/OpenGL window, BDF-only appearance and live
  session palette/fonts, real DBusMenu export and KDE Wayland AppMenu surface
  association. DomeD owns the menu bar; outside DomeD a local menu remains.
- Optional StatusNotifier item with an embedded ARGB icon and the same command
  menu for DomeD's tray manager. Hiding requires an accepted registration.
  Watcher disappearance restores hidden windows; a new watcher re-registers.
  Normal close remains an application command, not implicit hide-to-tray.
- Realm-aware desktop launcher delegates to the existing verified `dome-link`
  implementation. The library preserves the launching Wayland connection and
  never starts OpenXR or selects XR based on headset availability.
- Stable application identities in `include/artcraft/projects.hpp` without
  registering MIME defaults. The five test editors use these same identities.
- `ArtCraft::Documents`: validated test document models, bounded undo/redo,
  atomic native project saves and basic text/CSV/HTML/PPM/SVG exports.

The native library compiles the canonical DomeD menu exporter and includes the
canonical appearance/window headers directly. It does not carry independently
modified copies. Configure `ARTCRAFT_DOMED_SOURCE` to select another checkout;
keep that checkout and the patched FLTK runtime at mutually compatible versions.
This is a source-workspace dependency, not a standalone packaged SDK yet.

## Build and run

Requirements: CMake 3.21+, C++20 compiler, Ninja or another CMake generator,
DomeD's patched FLTK 1.4.5, BDF fonts/runtime, OpenGL/EGL, Fontconfig, FreeType,
zlib, nlohmann-json headers, GIO, Wayland client/scanner and X11 development metadata used by the shared
exporter's compatibility branch. No Qt/GTK toolkit is required. The installed
FLTK CMake metadata must refer to usable dependency paths.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/opt/fltk-vr/toolkit -DDOME_OFFICE_BUILD_LEGACY_FIXTURES=ON
cmake --build build
ctest --test-dir build --output-on-failure
PATH="$PWD/build:$PATH" python3 packaging/artcraft-demo
# Opt in to background tray lifetime:
PATH="$PWD/build:$PATH" python3 packaging/artcraft-demo --tray
```

Click the canvas or use Edit > Select or clear, then Edit > Apply. View > Grid
is a checkable action. File > Choose file exercises the native chooser without
claiming to import a document. Ctrl+W/Ctrl+Q close the demo. No document is saved
or edited and no persistent settings are changed by the demo.

The wrapper uses `dome-link` when installed and otherwise executes directly on
the caller's display. `FLTK_BACKEND` defaults to `wayland`; explicit caller
overrides are preserved for compatibility diagnostics. Applications remain
ordinary Wayland clients in DomeD's desktop/XR architecture.

To build only the headless command library and tests:

```sh
cmake -S . -B build-headless -DARTCRAFT_BUILD_NATIVE=OFF
cmake --build build-headless
ctest --test-dir build-headless --output-on-failure
```

## Reusing the base

Add this project with CMake `add_subdirectory` and link `ArtCraft::Platform` (or
`ArtCraft::Commands` for a headless engine). Construct `artcraft::Application`
with a stable application ID and title, then add commands and FLTK child widgets
before `show()` or `run()`. `window().begin()` / `end()` attach child widgets to
the owned window; start content at `content_top()` to accommodate the fallback
menu. The window owns its child widgets. Use `window().resizable(widget)` for
the app's content region. The base handles appearance and GL context resets;
application canvases own their retained GPU resources and context recreation.

Register `app.close` to implement the document's save/cancel policy. A disabled
close action leaves the window open. Without that command, window close exits.
Call `quit()` only after the document policy permits it. A tray-capable app can
expose its own restore/hide commands using `show()` and `hide_to_tray()`; the
latter returns false when no working tray is available. `run()` remains alive
when a tray window is hidden and exits on `quit()`.

State callbacks must be fast and nonthrowing. Commands and the FLTK API are
called on the UI thread. Register commands once before startup; enabled and
checked states may change afterward. This first API supports one application
lifecycle per process; multi-document activation and per-view focus/menu
selection are future work. Use a headless engine for document state, workers
for long operations and UI-thread completions for results.

The wrapper prepares routing but the base does not yet implement single-instance
D-Bus activation, document argument forwarding or live movement between realms.
Do not advertise those features for future apps until the activation layer is
implemented and verified.

## Verification and staging

The opt-in native test opens a temporary window on the caller's real Wayland
display and uses a private D-Bus session with a controlled tray watcher:

```sh
/usr/bin/python3 tests/native.py build/artcraft-base-demo
desktop-file-validate packaging/org.domed.ArtCraft.BaseDemo.desktop
cmake --install build --prefix "$PWD/build/stage"
```

Python GObject introspection (`gi`, Gio and GLib) and `dbus-run-session` are needed
for the native test. It checks the real exported command tree, disabled action
dispatch, selection changes, toggles, registration, hide/restore, watcher loss
and restart, and clean exit. It requires a hardware GL renderer and records the
selected BDF family/strike and renderer. It is separate from default headless
CTest checks because it depends on a running native desktop. Nothing installs
to system paths unless explicitly requested. The default stage contains the demo,
five test editors, their wrappers/desktop entries and license notices. Use
`--component wordcraft` (or another project name) to stage only that editor.
Each test editor also has a separate archive package.

On 2026-11-11 the headless checks and native suite passed using FLTK 1.4.5,
`UFO16 / Sans-Serif` at 16 pixels and `Mesa Intel(R) Graphics (ADL-N)`. Linkage
inspection found no Qt or GTK libraries. The inherited session D-Bus socket
refused connections, so menu/tray testing used a private bus. The shared exporter
provides Wayland menu association, but actual shell-menu clicking, physical XR,
live appearance changes, native chooser interaction and cross-GPU DMA-BUF
transport have not been certified by this test.

The GPU claim is limited: the demo uses FLTK's GPU widget path on native Wayland.
Glyph rasterization and icon generation can use the CPU. This is not evidence
that every document operation or compositor transport path is GPU-only.
The five test models add basic editing, formulas, saves and exports as described
in `TEST-APPS.md`. Rich document layout, production formula compatibility,
tiled/layered editing, recovery and accessibility remain subsequent work.
