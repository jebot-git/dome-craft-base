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
History and channel actions operate on the same document. PhotoCraft does not use the generic command-form dispatcher. Image/Canvas Size,
New Document, color picking, layer properties/blending, selection modifiers,
fill/stroke, Gaussian Blur, channels and the main adjustments use dedicated
native controls. Curves has an editable graph; text is entered on the canvas;
Pen and Path use direct canvas gestures. Parameterized actions without a native
controller remain disabled. Internal automation commands are not application
menu items; the upstream command API remains available to automation.

The tool-options strip changes with the tool: marquee shape, selection mode,
feather and anti-aliasing; wand tolerance, contiguous and sample-all-layers;
quick-selection size; brush size/mode/opacity/hardness; aligned Clone Stamp;
and real Blur strength/Dodge exposure. Right-click a grouped tool or use its
options dropdown to choose a subtool; Shift+the tool letter cycles subtools.
Context menus and choice dropdowns are native child widgets drawn in the same
GPU window, including modal-editor dropdowns and the color chooser. Right-click
opens canvas, Layers, Channels, Paths and History actions. Hold a grouped tool
for its flyout. Keyboard navigation, checked/disabled entries, scrolling and
outside-click/Escape dismissal share the same menu implementation. Right-click
on a layer selects it without toggling its eye; menu state is taken after that
selection reaches the engine.

Crop gestures show a rule-of-thirds preview and require Crop/Enter to commit;
Cancel/Escape leaves the document intact. Type has an inline editor with size,
alignment, Commit/Cancel and Ctrl+Enter/Escape. Pen clicks create a work path;
Enter finishes it, clicking its starting point closes it, and Path drags translate
it. Background/foreground swatches open a visual color chooser.

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
engine mask or document pixels. Selection borders use explicit one-pixel
black/white bands with a shared animated phase. They do not depend on GPU line
stipple, which the native toolkit's axis-aligned rectangle fast path ignores. The small mask is also used for outline-hit
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
clicks, Escape, full-canvas edges, polygon lasso, wand and consecutive strokes. It also dispatches real window-level pointer events
through flyouts, choice dropdowns (including modal ones), and canvas/layer
context menus; verifies dedicated image-size controls, inline type commit,
graphical Pen/Path, confirmed crop, all twelve adjustment controllers and
alpha-channel creation/deletion. GPU framebuffer readback additionally verifies
visible black/white selection pixels on light and dark images at 50/100/300%
zoom, plus actual pixel changes as the selection ants animate.
The engine unit regression verifies holes, bounded preview size and read-only
inspection.

Native tests exercise the real engine through exported menus: opening a generated
image, creating/duplicating/deleting layers, painting, undo/redo, zoom, dock
visibility, layered `.pcraft` save and fresh-process reopen. Captures come from
the native GPU window. The thumbnail regression verifies isolation and unchanged
inspection/history, and rejects unknown layers and oversized requests.

The main-window regions are implemented; this is not certification of complete
Photoshop feature or bespoke-dialog parity. Bezier-handle/direct-anchor manipulation, advanced layer effects and other
parameterized actions still need dedicated editors. Those actions are disabled
when no native controller exists; there is no command-form fallback. The upstream automation
preview is bounded to 64 Mi source pixels and a 2048px output edge; layer previews
have a stricter 256px edge. Physical XR/tablet interaction and production file
compatibility have not been certified by these desktop checks.
