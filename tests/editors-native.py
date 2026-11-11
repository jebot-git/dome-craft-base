#!/usr/bin/python3
"""Test all five editors on native Wayland with an isolated D-Bus session."""
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

if not os.environ.get("ARTCRAFT_EDITORS_PRIVATE_BUS"):
    raise SystemExit(subprocess.call(
        ["dbus-run-session", "--", sys.executable, __file__, *sys.argv[1:]],
        env={**os.environ, "ARTCRAFT_EDITORS_PRIVATE_BUS": "1"}))

from gi.repository import Gio, GLib

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
results = []
captures = build / "editor-captures"
captures.mkdir(parents=True, exist_ok=True)
names = ["wordcraft", "gridcraft", "deckcraft", "photocraft", "vectorcraft"]
titles = ["WordCraft", "GridCraft", "DeckCraft", "PhotoCraft", "VectorCraft"]
exports = [".txt", ".csv", ".html", ".ppm", ".svg"]
operations = ["Insert paragraph", "Insert example", "Add slide", "Invert colors", "Add rectangle"]
seeds = [
    {"text": "Seed text — 日本語\n"},
    {"cells": [["" for _ in range(8)] for _ in range(16)]},
    {"slides": [{"title": "Seed <title>", "body": "Text & 日本語"}]},
    {"width": 64, "height": 48, "pixels": [0, 128, 255] + [255] * (64 * 48 * 3 - 3)},
    {"shapes": [{"x": 10, "y": 20, "width": 30, "height": 40, "color": [1, 2, 3]}]},
]


def wait_for(predicate, message, process, errors, timeout=7):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            errors.seek(0)
            raise AssertionError(f"{message}: process exited {process.returncode}\n{errors.read()}")
        if predicate():
            return
        time.sleep(.035)
    raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix="artcraft-editors-") as temporary:
    directory = Path(temporary)
    for name, title, suffix, operation, seed in zip(names, titles, exports, operations, seeds):
        project = directory / f"日本語 test.{name}"
        project.write_text(json.dumps({"format": "artcraft-test", "version": 1,
                                       "application": name, "content": seed}))
        export = directory / (name + suffix)
        report = directory / f"{name}-report.json"
        capture = captures / f"{name}.ppm"
        expected = None
        checks = []
        for phase in ["edit", "reopen"]:
            report.unlink(missing_ok=True)
            with (directory / f"{name}-{phase}.log").open("w+") as errors:
                process = subprocess.Popen([str(build / f"{name}-native"), str(project),
                    "--report", str(report), "--export", str(export), "--capture", str(capture), "--quit-after-ms", "30000"],
                    stderr=errors, env={**os.environ, "FLTK_BACKEND": "wayland"})
                try:
                    def snapshot():
                        try:
                            return json.loads(report.read_text())
                        except (OSError, ValueError):
                            return {}

                    wait_for(lambda: snapshot().get("menu_service"), f"{name} startup", process, errors)
                    info = snapshot()
                    assert info["app_id"] == "org.domed.ArtCraft." + title
                    assert info["renderer"] and "llvmpipe" not in info["renderer"].lower()
                    assert info["font_pixels"] > 0 and info["font"]
                    service, path = info["menu_service"], info["menu_path"]

                    def rows():
                        layout = bus.call_sync(service, path, "com.canonical.dbusmenu", "GetLayout",
                            GLib.Variant("(iias)", (0, -1, [])), None,
                            Gio.DBusCallFlags.NONE, 2000, None).unpack()[1]
                        found = {}

                        def visit(node):
                            number, properties, children = node
                            found[properties.get("label", "").replace("_", "")] = (number, properties)
                            for child in children:
                                visit(child)
                        visit(layout)
                        return found

                    def click(label):
                        entry = rows()[label]
                        assert entry[1].get("enabled", True), f"{name}: disabled {label}"
                        bus.call_sync(service, path, "com.canonical.dbusmenu", "Event",
                            GLib.Variant("(isvu)", (entry[0], "clicked", GLib.Variant("i", 0), 0)),
                            None, Gio.DBusCallFlags.NONE, 2000, None)

                    if phase == "edit":
                        checks.append("Native GPU/BDF window and app-specific menu")
                        assert not rows()["Undo"][1]["enabled"]
                        click(operation)
                        wait_for(lambda: snapshot().get("dirty"), f"{name} menu edit", process, errors)
                        expected = snapshot()["content"]
                        assert expected != seed
                        if name == "wordcraft": assert expected["text"].endswith("A native WordCraft paragraph.\n")
                        if name == "gridcraft": assert snapshot()["C1"] == "8"
                        if name == "deckcraft": assert len(expected["slides"]) == 2
                        if name == "photocraft": assert expected["pixels"][:3] == [255, 127, 0]
                        if name == "vectorcraft": assert len(expected["shapes"]) == 2
                        checks.append("Project-specific editing changes its model")
                        click("Undo")
                        wait_for(lambda: snapshot().get("content") == seed and not snapshot().get("dirty"),
                                 f"{name} undo", process, errors)
                        click("Redo")
                        wait_for(lambda: snapshot().get("content") == expected and snapshot().get("dirty"),
                                 f"{name} redo", process, errors)
                        checks.append("Undo/redo restores model and dirty state")
                        click("Save")
                        wait_for(lambda: not snapshot().get("dirty", True), f"{name} save", process, errors)
                        saved = json.loads(project.read_text())
                        assert saved["content"] == expected
                        checks.append("Saved project independently decoded")
                        click("Export...")
                        wait_for(export.exists, f"{name} export", process, errors)
                        if name == "wordcraft": assert export.read_text() == expected["text"]
                        if name == "gridcraft": assert list(csv.reader(io.StringIO(export.read_text())))[0][:3] == ["2", "3", "8"]
                        if name == "deckcraft":
                            html = export.read_text()
                            assert html.count("<section>") == 2 and "Seed &lt;title&gt;" in html and "Text &amp; 日本語" in html
                        if name == "photocraft":
                            data = export.read_bytes()
                            assert data.startswith(b"P6\n64 48\n255\n") and data[13:16] == bytes([255, 127, 0])
                            assert len(data) == 13 + 64 * 48 * 3
                        if name == "vectorcraft":
                            root = ET.parse(export).getroot()
                            assert len(root.findall("{http://www.w3.org/2000/svg}rect")) == 2
                        checks.append("Export independently parsed and checked")
                        def captured_pixel(px, py):
                            try:
                                header, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
                                width, height = map(int, dimensions.split())
                                assert header == b"P6" and maximum == b"255" and len(pixels) == width * height * 3
                                # The app reports logical pixels; captures use physical pixels.
                                window = snapshot()["window"]
                                x, y = int(px * width / window[0]), int(py * height / window[1])
                                return tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3])
                            except (OSError, ValueError):
                                return None
                        wait_for(lambda: captured_pixel(100, 100) is not None,
                                 f"{name} GL framebuffer capture", process, errors)
                        if name == "photocraft":
                            cx, cy, cw, ch = snapshot()["canvas"]
                            wait_for(lambda: captured_pixel(cx + cw / 2, cy + ch / 2) == (0, 0, 0),
                                     "PhotoCraft texture displays inverted pixels", process, errors)
                            wait_for(lambda: captured_pixel(cx + cw / 128, cy + ch / 96) == (255, 127, 0),
                                     "PhotoCraft texture preserves RGB channels and top-left orientation", process, errors)
                        if name == "vectorcraft":
                            cx, cy, cw, ch = snapshot()["canvas"]
                            wait_for(lambda: captured_pixel(cx + cw * 174 / 640, cy + ch * 124 / 400) == (107, 171, 255),
                                     "VectorCraft canvas displays the added rectangle", process, errors)
                        checks.append("Actual GL framebuffer captured; graphic colors checked")
                    else:
                        assert info["content"] == expected and not info["dirty"]
                        checks.append("Fresh process reopens exact saved model")
                    click("Quit")
                    process.wait(timeout=5)
                    assert process.returncode == 0
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
        results.append({"application": name, "renderer": info["renderer"], "font": info["font"], "checks": checks})

print(json.dumps({"applications": results}, indent=2))
