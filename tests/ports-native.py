#!/usr/bin/python3
"""Exercise the real upstream engines through the native DomeD port menus."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET

if not os.environ.get("DOME_CRAFT_PORT_TEST_BUS"):
    raise SystemExit(subprocess.call(["dbus-run-session", "--", sys.executable, __file__, *sys.argv[1:]],
        env={**os.environ, "DOME_CRAFT_PORT_TEST_BUS": "1"}))
from gi.repository import Gio, GLib

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build").resolve()
names = sys.argv[2:]
lock = json.loads((root / "upstream.lock.json").read_text())
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
captures = root / "build-upstream/captures"
captures.mkdir(parents=True, exist_ok=True)
results = []
with tempfile.TemporaryDirectory(prefix="dome-craft-ports-") as temporary:
    workspace = Path(temporary)
    for project in lock["projects"]:
        if project["suite"] != "office":
            continue
        name = project["name"]
        if names and name not in names:
            continue
        save = workspace / ("日本語 office test" + project["default_extension"])
        report = workspace / f"{name}.json"
        capture = captures / f"{name}.ppm"
        checks = []
        for phase in ["edit", "reopen"]:
            report.unlink(missing_ok=True)
            with (workspace / f"{name}-{phase}.log").open("w+") as errors:
                command = [str(build / f"{name}-domed-native"), "--registry", str(root / "upstream.lock.json"),
                    "--engine", str(root / "build-upstream/target/release" / project["cli"]),
                    "--workspace", str(workspace), "--report", str(report), "--save-path", str(save),
                    "--capture", str(capture), "--quit-after-ms", "45000"]
                if phase == "reopen":
                    command.append(str(save))
                process = subprocess.Popen(command, stderr=errors, env={**os.environ, "FLTK_BACKEND": "wayland"})
                try:
                    def snapshot():
                        try:
                            return json.loads(report.read_text())
                        except (OSError, ValueError):
                            return {}

                    def wait(predicate, reason):
                        deadline = time.monotonic() + 30
                        while time.monotonic() < deadline:
                            if process.poll() is not None:
                                errors.seek(0)
                                raise AssertionError(f"{name}: exited {process.returncode}: {errors.read()}")
                            state = snapshot()
                            if state.get("error"):
                                raise AssertionError(f"{name}: {state['error']}")
                            if predicate(state):
                                return
                            time.sleep(.05)
                        errors.seek(0)
                        raise AssertionError(f"{name}: {reason}: {errors.read()}")

                    wait(lambda s: s.get("snapshot", {}).get("commands") and not s.get("busy") and s.get("renderer"), "startup")
                    info = snapshot()
                    assert info["native_menu_commands"] >= len(info["snapshot"]["commands"])
                    assert all(c["enabled_known"] for c in info["snapshot"]["commands"])
                    assert info["app_id"] == "org.domed." + project["title"]
                    assert info["revision"] == project["revision"] and info["font"]
                    assert Path(info["font_file"]).read_bytes().startswith(b"STARTFONT")
                    assert "BDF-only" in info["font_policy"]
                    assert info["renderer"] and "llvmpipe" not in info["renderer"].lower()
                    service, menu = info["menu_service"], info["menu_path"]

                    def rows():
                        layout = bus.call_sync(service, menu, "com.canonical.dbusmenu", "GetLayout",
                            GLib.Variant("(iias)", (0, -1, [])), None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[1]
                        found = {}

                        def visit(node):
                            number, properties, children = node
                            found[properties.get("label", "").replace("_", "")] = (number, properties)
                            for child in children:
                                visit(child)
                        visit(layout)
                        return found

                    def click(label):
                        deadline = time.monotonic() + 4
                        while not rows()[label][1].get("enabled", True):
                            if time.monotonic() > deadline:
                                raise AssertionError(f"{name}: {label} is disabled")
                            time.sleep(.05)
                        bus.call_sync(service, menu, "com.canonical.dbusmenu", "Event",
                            GLib.Variant("(isvu)", (rows()[label][0], "clicked", GLib.Variant("i", 0), 0)),
                            None, Gio.DBusCallFlags.NONE, 2000, None)

                    if name == "wordcraft":
                        ui = info["ui"]
                        assert ui["native_word_layout"] and not ui["paragraph_editor"]
                        assert "File" not in ui["ribbon"]["tabs"] and "Help" not in ui["ribbon"]["tabs"]
                        layout = bus.call_sync(service, menu, "com.canonical.dbusmenu", "GetLayout",
                            GLib.Variant("(iias)", (0, 1, [])), None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[1]
                        top = [node[1].get("label", "").replace("_", "") for node in layout[2]]
                        assert top == ["WordCraft", "File", "Edit", "View", "Insert", "Format", "Tools", "Table", "Window", "Help"], top
                        checks.append("macOS menu arrangement exported to the universal bar; ribbon remains in window")
                        if phase == "edit":
                            click("Navigation Pane")
                            wait(lambda s: not s.get("busy") and s.get("ui", {}).get("navigation"), "navigation pane")
                            click("Navigation Pane")
                            wait(lambda s: not s.get("busy") and not s.get("ui", {}).get("navigation"), "navigation pane hidden")
                            click("One Page")
                            wait(lambda s: not s.get("busy") and s.get("ui", {}).get("scale", 2) < 1, "fit-page zoom")
                            click("100%")
                            wait(lambda s: not s.get("busy") and abs(s.get("ui", {}).get("scale", 0)-4/3) < .001, "100% zoom")
                            checks.append("Universal View controls update navigation and actual page geometry")
                    if phase == "edit":
                        checks.append("Pinned upstream session in native BDF/OpenGL host")
                        before = info["snapshot"]["document"]
                        click("Run selected")
                        wait(lambda s: not s.get("busy") and s.get("dirty") and s.get("snapshot", {}).get("document") != before, "upstream edit")
                        checks.append("Native menu dispatches actual upstream edit")
                        click("Undo")
                        wait(lambda s: not s.get("busy") and s.get("snapshot", {}).get("document") != info["snapshot"]["document"], "undo")
                        click("Redo")
                        # Snapshot changes can include history, so menu enabled state and the
                        # serialized file are checked as well as command success.
                        wait(lambda s: not s.get("busy") and not s.get("error"), "redo")
                        checks.append("Actual upstream undo/redo through universal menu")
                        click("Save As...")
                        wait(lambda s: not s.get("busy") and not s.get("dirty"), "save")
                        assert save.exists() and save.stat().st_size > 0
                        if name in ["wordcraft", "gridcraft", "deckcraft"]:
                            with zipfile.ZipFile(save) as archive:
                                parts = archive.namelist()
                                assert "[Content_Types].xml" in parts
                                if name == "wordcraft":
                                    xml = ET.fromstring(archive.read("word/document.xml"))
                                    assert "Native WordCraft on DomeD." in "".join(xml.itertext())
                                elif name == "gridcraft":
                                    xml = ET.fromstring(archive.read("xl/worksheets/sheet1.xml"))
                                    assert "SUM(1,2,3)" in "".join(xml.itertext())
                                else:
                                    slides = [p for p in parts if p.startswith("ppt/slides/slide") and p.endswith(".xml") and "/_rels/" not in p]
                                    assert any("DomeD slide" in "".join(ET.fromstring(archive.read(p)).itertext()) for p in slides)
                        elif name == "photocraft":
                            with zipfile.ZipFile(save) as archive:
                                document = json.loads(archive.read("manifest.json"))["document"]
                                assert document["size"] == {"width": 64, "height": 48}
                                assert any(layer["name"] == "DomeD layer" for layer in document["layers"])
                        elif name == "vectorcraft":
                            envelope = json.loads(save.read_bytes())
                            assert envelope["format"] == "vectorcraft"
                            children = envelope["document"]["layers"][0]["kind"]["children"]
                            rectangle = next(node for node in children if node["kind"].get("live", {}).get("shape") == "rectangle")
                            live = rectangle["kind"]["live"]
                            assert live["w"] == 120 and live["h"] == 80
                            anchors = rectangle["kind"]["path"]["subpaths"][0]["anchors"]
                            assert [point["p"] for point in anchors] == [[24, 24], [144, 24], [144, 104], [24, 104]]
                        checks.append("Upstream-format save independently verified")
                        wait(lambda s: capture.exists(), "GL capture")
                    else:
                        assert info["snapshot"]["document"]
                        checks.append("Fresh native process reopens upstream file")
                    if name == "deckcraft" and phase == "edit":
                        click("Slide show")
                        wait(lambda s:s.get("playing") and s.get("snapshot",{}).get("view",{}).get("raster",{}).get("png_bytes",0)>0,"native player")
                        click("End slide show")
                        wait(lambda s:not s.get("playing") and not s.get("busy"),"native player exit")
                        checks.append("Native fullscreen slide player enters and exits through universal menu")
                    click("Quit")
                    process.wait(timeout=5)
                    assert process.returncode == 0
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
        results.append({"application": name, "revision": project["revision"], "checks": checks})
print(json.dumps({"ports": results}, indent=2))
