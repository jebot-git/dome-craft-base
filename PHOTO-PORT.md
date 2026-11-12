# PhotoCraft for DomeD

PhotoCraft is a separate image editor over the pinned Storytold engine. The office
suite remains WordCraft, GridCraft and DeckCraft; VectorCraft remains excluded.
The ArtCraft AI application is not a dependency.

## Native main window

The macOS arrangement exports application and document menus through DomeD's
universal menu. The window uses the Photoshop workspace arrangement: a context
options bar, a two-column toolbox, document tabs, a centered/pannable image canvas
with rulers and a zoom/status bar, a narrow panel rail and a dock on the right.
The dock contains Color and swatches, Navigator, Histogram and History; Properties
and Adjustments; and Layers, Channels and Paths. Dock boundaries can be resized,
panels hidden, and the Essentials workspace restored through Window actions.
DomeD supplies decorations, palette, bitmap fonts, tray/menu infrastructure,
native GPU windows and desktop/XR routing. No macOS decorations are imitated.
Tool icons are original native geometry, with no imported Photoshop assets.

The native canvas consumes the engine's actual PNG composite. Pointer gestures
invoke its paint, selection, layer translation, crop, gradient, shape/frame and
type operations. The Layers panel has genuine isolated layer previews, selection,
visibility, blend mode, opacity, locks, masks, grouping, creation and deletion.
History and channel actions operate on the same document. Compound operations
remain available in native parameter dialogs from the command catalog.

The tool-options strip changes with the tool: marquee shape, selection mode,
feather and anti-aliasing; wand tolerance, contiguous and sample-all-layers;
quick-selection size; brush size/mode/opacity/hardness; aligned Clone Stamp;
and real Blur strength/Dodge exposure. Right-click a grouped tool or use its
options dropdown to choose a subtool; Shift+the tool letter cycles subtools.
The strip scrolls horizontally in compact windows and the workspace picker
hides before it can overlap editing controls.

Rectangular, elliptical, single-row/column, freehand/polygonal lasso, Magic Wand
and Quick Selection gestures dispatch the upstream commands. Shift adds, Alt
subtracts and Shift+Alt intersects; modifiers are captured at press. Dragging
inside a selection moves its outline; Move with a selection floats its pixels.
Polygonal lasso commits with Enter/double-click. Escape cancels pending gestures.
A simple marquee click deselects; clicks outside the image do not paint/select
its edge, and selections can reach the final image row/column. Rapid strokes
are queued separately so each remains an independent engine edit.

`document.inspect.selectionPreview` is a read-only bitmap of actual selection
coverage, bounded to 256 rows/columns. Native black/white outlines follow that
mask, including curves, disjoint pieces and holes, instead of a bounding box.
Large-image feedback is sampled; this does not change the full-resolution
engine mask or document pixels. The small mask is also used for outline-hit
feedback. Frame creates a genuine frame layer, rather than a filled rectangle.

A bounded optional `layer` field on the headless `render_preview` tool supplies
1–256px thumbnails without changing the document, layer visibility, selection or
history. It clones the document's shared metadata, isolates the requested layer
and uses the upstream compositor. The source and PNG budgets remain enforced.
The UI caches at most 64 layer previews and requests the visible layer range.
The histogram is computed from the displayed composite preview, not full source
precision. Office text layout is bitmap-only; PhotoCraft uses bitmap-only native
UI text while retaining the upstream image/type-layer renderer for document
content. No font assets are bundled.

## Build and stage

From the shared base checkout, with canonical DomeD sources and its patched FLTK
1.4.5 toolkit available:

```sh
python3 scripts/upstream.py fetch photocraft
python3 scripts/upstream.py build photocraft
cmake -S . -B build-office -DCMAKE_BUILD_TYPE=Release \
  -DARTCRAFT_DOMED_SOURCE=/path/to/DomeD \
  -DCMAKE_PREFIX_PATH=/opt/fltk-vr/toolkit
cmake --build build-office --target photocraft-domed-native -j4
python3 tests/photo-native.py build-office
python3 scripts/stage-domed-ports.py build-office/photo-stage \
  --build build-office --suite photo
```

Use an empty stage. `--suite office` packages only the three office apps;
`--suite all` adds PhotoCraft. PhotoCraft staging does not replace image-viewer
MIME defaults. Source licenses, the TIFF build fix and native thumbnail patch
are retained and hashed in `upstream.lock.json`.

## Validation and limits

The preview-error regression also verifies that failed raster generation preserves
the authoritative dirty state and save destination.

`dome-photo-tools-test LOCK ENGINE WORKSPACE` additionally drives native mouse,
keyboard and tool-option callbacks against the real engine: selection geometry,
intersection/modifier release, elliptical masks, outline movement, outside-image
clicks, Escape, full-canvas edges, polygon lasso, wand and consecutive strokes.
The engine unit regression verifies holes, bounded preview size and read-only
inspection.

Native tests exercise the real engine through exported menus: opening a generated
image, creating/duplicating/deleting layers, painting, undo/redo, zoom, dock
visibility, layered `.pcraft` save and fresh-process reopen. Captures come from
the native GPU window. The thumbnail regression verifies isolation and unchanged
inspection/history, and rejects unknown layers and oversized requests.

The main-window regions are implemented; this is not certification of complete
Photoshop feature or bespoke-dialog parity. Advanced pen/path interactions and
compound command parameters still use native forms. The upstream automation
preview is bounded to 64 Mi source pixels and a 2048px output edge; layer previews
have a stricter 256px edge. Physical XR/tablet interaction and production file
compatibility have not been certified by these desktop checks.
