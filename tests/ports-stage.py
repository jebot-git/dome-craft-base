#!/usr/bin/python3
"""Verify staged native ports, file associations and relocated wrappers."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build").resolve()
suite = sys.argv[2] if len(sys.argv) > 2 else "office"
with tempfile.TemporaryDirectory(prefix="dome-craft-stage-") as temporary:
    directory = Path(temporary)
    original = directory / "dome-mimeapps.list"
    original.write_text('''[Default Applications]
application/pdf=miro-pdf.desktop;
image/png=qimgvr.desktop;
application/vnd.openxmlformats-officedocument.wordprocessingml.document=calligra-words-fltk.desktop;other-editor.desktop;
application/vnd.oasis.opendocument.presentation=calligra-stage-fltk.desktop;
application/vnd.oasis.opendocument.spreadsheet=calligra-sheets-fltk.desktop;fallback-sheet.desktop;
''')
    digest = hashlib.sha256(original.read_bytes()).hexdigest()
    stage = directory / "prefix"
    subprocess.run([sys.executable, str(root / "scripts/stage-domed-ports.py"), str(stage),
        "--build", str(build), "--suite", suite, "--mimeapps", str(original)], check=True, stdout=subprocess.DEVNULL)
    assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
    manifest = json.loads((stage / "manifest.json").read_text())
    assert not manifest["installed"]
    assert set(manifest["suite"]["office"]) == ({"wordcraft","gridcraft","deckcraft"} if suite in ["office","all"] else set())
    assert set(manifest["suite"]["art"]) == ({"photocraft"} if suite in ["photo","all"] else set())
    assert not any("vectorcraft" in str(p).lower() for p in (stage/"bin").iterdir())
    assert set(manifest["future"]) == {"soundcraft", "designcraft", "lightcraft"}
    for name, digest in manifest["sha256"].items():
        assert hashlib.sha256((stage / name).read_bytes()).hexdigest() == digest, name
    if suite in ["office","all"]:
        policy = configparser.ConfigParser(interpolation=None)
        policy.optionxform = str
        policy.read(stage / "integration/dome-mimeapps.list")
        defaults = policy["Default Applications"]
        assert defaults["application/pdf"] == "miro-pdf.desktop;"
        assert defaults["image/png"] == "qimgvr.desktop;"
        assert defaults["application/vnd.openxmlformats-officedocument.wordprocessingml.document"] == "org.domed.WordCraft.desktop;other-editor.desktop;"
        assert "application/vnd.oasis.opendocument.presentation" not in defaults
        assert defaults["application/vnd.oasis.opendocument.spreadsheet"] == "fallback-sheet.desktop;"
    else:
        assert not (stage / "integration/dome-mimeapps.list").exists()
    helper = directory / "dome-link"
    record = directory / "arguments.json"
    helper.write_text('''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
Path(os.environ["DOME_PORT_ARGS"]).write_text(json.dumps(sys.argv[1:]))
os.execv(sys.argv[2], sys.argv[2:])
''')
    helper.chmod(0o755)
    projects = manifest["suite"]["office"] + manifest["suite"]["art"]
    for name in projects:
        launcher = stage / "bin" / f"{name}-domed"
        arguments = ["--help", "日本語 with spaces", "$(touch never-execute)"]
        result = subprocess.run([str(launcher), *arguments], text=True, capture_output=True,
            env={**os.environ, "PATH": str(directory), "DOME_PORT_ARGS": str(record)}, check=True)
        assert name.lower() in result.stdout.lower()
        forwarded = json.loads(record.read_text())
        assert forwarded[0] == "--" and forwarded[1] == str(stage / "bin" / f"{name}-domed-native")
        assert forwarded[-3:] == arguments
        for binary in [stage / "bin" / f"{name}-domed-native", stage / "libexec/dome-craft" / f"{name}-cli"]:
            linked = subprocess.check_output(["ldd", str(binary)], text=True)
            assert not re.search(r"lib(?:Qt[456]|gtk[-.])|not found", linked), linked
    desktops = list((stage / "share/applications").glob("*.desktop"))
    assert len(desktops) == len(projects)
    assert all("ArtCraft" not in p.name for p in desktops)
    print(json.dumps({"suite": suite, "applications": projects, "checks": [
        "Native and upstream binaries stage with matching hashes",
        "Only the selected applications have launchers; VectorCraft stays excluded",
        "Future targets remain registry-only",
        "Original MIME preferences remain byte-identical",
        "Office defaults replace supported Calligra handlers; PDF/image preferences preserved",
        "Unsupported ODF types are not falsely assigned",
        "Relocated launchers preserve exact arguments and delegate realm selection",
        "Native hosts and backend binaries have no missing, Qt or GTK libraries"]}, indent=2))
