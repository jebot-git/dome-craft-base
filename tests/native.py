#!/usr/bin/python3
"""Exercise real native Wayland rendering and D-Bus commands on a private bus.

Uses the caller's Wayland display; opens one temporary demo window. No session
services are restarted and no persistent desktop settings are changed.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if not os.environ.get("ARTCRAFT_TEST_PRIVATE_BUS"):
    raise SystemExit(subprocess.call(
        ["dbus-run-session", "--", sys.executable, __file__, *sys.argv[1:]],
        env={**os.environ, "ARTCRAFT_TEST_PRIVATE_BUS": "1"}))

from gi.repository import Gio, GLib

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "build/artcraft-base-demo").resolve()
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
context = GLib.MainContext.default()
checks = []


def wait_for(predicate, message, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        while context.pending():
            context.iteration(False)
        if predicate():
            checks.append(message)
            return
        time.sleep(.025)
    raise AssertionError(message)


def call(service, path, interface, method, parameters=None):
    return bus.call_sync(service, path, interface, method, parameters,
                         None, Gio.DBusCallFlags.NONE, 2000, None)


watcher_info = Gio.DBusNodeInfo.new_for_xml('''<node>
<interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
</interface></node>''')
registrations = []


def register_item(connection, sender, path, interface, method, parameters, invocation):
    registrations.append(parameters.unpack()[0])
    invocation.return_value(None)


registration = bus.register_object("/StatusNotifierWatcher", watcher_info.interfaces[0],
                                   register_item, None, None)
watch = 0
process = None
try:
    with tempfile.TemporaryDirectory(prefix="artcraft-native-") as directory:
        report = Path(directory) / "report.txt"
        with (Path(directory) / "stderr.txt").open("w+") as errors:
            process = subprocess.Popen([str(binary), "--tray", "--report", str(report),
                                        "--quit-after-ms", "25000"], stderr=errors,
                                       env={**os.environ, "FLTK_BACKEND": "wayland"})

            def snapshot():
                if process.poll() is not None:
                    errors.seek(0)
                    raise AssertionError(f"Demo exited {process.returncode}: {errors.read()}")
                try:
                    lines = report.read_text().splitlines()
                except FileNotFoundError:
                    return []
                return lines if len(lines) == 11 else []

            wait_for(lambda: bool(snapshot()) and bool(snapshot()[1]),
                     "Native window and exported DBusMenu started")
            info = snapshot()
            service, menu = info[1:3]
            assert info[0] == "org.domed.ArtCraft.BaseDemo"
            assert int(info[4]) > 0 and info[3]
            assert info[5] and "llvmpipe" not in info[5].lower(), info[5]
            checks.append("Native bitmap family/strike and hardware GL renderer reported")

            def rows():
                result = call(service, menu, "com.canonical.dbusmenu", "GetLayout",
                              GLib.Variant("(iias)", (0, -1, []))).unpack()[1]
                found = {}

                def visit(node):
                    number, properties, children = node
                    found[properties.get("label", "").replace("_", "")] = (number, properties)
                    for child in children:
                        visit(child)
                visit(result)
                return found

            def click(label):
                number = rows()[label][0]
                call(service, menu, "com.canonical.dbusmenu", "Event",
                     GLib.Variant("(isvu)", (number, "clicked", GLib.Variant("i", 0), 0)))

            assert not rows()["Apply"][1]["enabled"]
            click("Apply")
            time.sleep(.15)
            assert snapshot()[8] == "0"
            checks.append("Disabled exported action cannot edit the model")
            click("Select or clear")
            wait_for(lambda: rows()["Apply"][1]["enabled"], "Selection enables exported action")
            click("Apply")
            wait_for(lambda: snapshot()[8] == "1", "Exported action invokes the shared command")
            click("Grid")
            wait_for(lambda: rows()["Grid"][1]["toggle-state"] == 0 and snapshot()[9] == "0",
                     "Checkable exported action updates model and menu")
            assert not rows()["Hide to tray"][1]["enabled"]
            checks.append("Missing tray watcher leaves hide disabled")

            watch = Gio.bus_own_name_on_connection(bus, "org.kde.StatusNotifierWatcher",
                                                  Gio.BusNameOwnerFlags.NONE, None, None)
            wait_for(lambda: bool(registrations) and snapshot()[6] == "1",
                     "Optional tray registers when a watcher appears")
            assert registrations[-1] == service
            tray_menu = call(service, "/StatusNotifierItem", "org.freedesktop.DBus.Properties",
                            "Get", GLib.Variant("(ss)", ("org.kde.StatusNotifierItem", "Menu")))
            assert tray_menu.unpack()[0] == menu
            checks.append("Tray manager receives the actual application command tree")
            wait_for(lambda: rows()["Hide to tray"][1]["enabled"], "Tray acceptance enables hiding")
            click("Hide to tray")
            wait_for(lambda: snapshot()[10] == "0", "Window hides while tray service remains responsive")
            call(service, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate",
                 GLib.Variant("(ii)", (0, 0)))
            wait_for(lambda: snapshot()[10] == "1", "Tray activation restores the GL window")
            click("Hide to tray")
            wait_for(lambda: snapshot()[10] == "0", "Restored window can hide again")
            Gio.bus_unown_name(watch)
            watch = 0
            wait_for(lambda: snapshot()[10] == "1" and snapshot()[6] == "0",
                     "Watcher loss restores a hidden window instead of stranding it")
            watch = Gio.bus_own_name_on_connection(bus, "org.kde.StatusNotifierWatcher",
                                                  Gio.BusNameOwnerFlags.NONE, None, None)
            wait_for(lambda: len(registrations) == 2 and snapshot()[6] == "1",
                     "Watcher restart re-registers the tray item")
            click("Quit")
            process.wait(timeout=5)
            assert process.returncode == 0
            checks.append("Quit shuts down cleanly after repeated hide/restore")
            print(json.dumps({"renderer": info[5], "font": info[3], "checks": checks}, indent=2))
finally:
    if watch:
        Gio.bus_unown_name(watch)
    bus.unregister_object(registration)
    if process is not None and process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
