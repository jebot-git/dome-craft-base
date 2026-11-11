#!/usr/bin/python3
"""Stage and archive five native test packages without modifying the host."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--output", type=Path, default=Path("build/packages"))
args = parser.parse_args()
build, output = args.build.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
names = ["wordcraft", "gridcraft", "deckcraft", "photocraft", "vectorcraft"]
for name in names:
    with tempfile.TemporaryDirectory(prefix=f"artcraft-{name}-") as temporary:
        stage = Path(temporary) / f"{name}-test-0.1.0"
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(stage),
                        "--component", name], check=True, stdout=subprocess.DEVNULL)
        files = {}
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                files[str(path.relative_to(stage))] = hashlib.sha256(path.read_bytes()).hexdigest()
        manifest = {"application": name, "version": "0.1.0-test", "architecture": platform.machine(),
                    "requires": ["DomeD patched FLTK 1.4.5 at the configured runtime prefix",
                                 "Installed DomeD BDF runtime and Unicode BDF fonts",
                                 "Native Wayland/EGL/OpenGL, GIO, Fontconfig and Python 3"],
                    "format": f".{name} (ArtCraft test JSON v1)", "sha256": files}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        # Keep build products relocatable within the configured host runtime.
        # Private FLTK libraries are external requirements, not silently bundled.
        archive = output / f"{stage.name}-{platform.machine()}.tar.gz"
        with tarfile.open(archive, "w:gz") as package:
            package.add(stage, arcname=stage.name)
        print(archive)
checksums = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(output.glob("*-test-0.1.0-*.tar.gz"))}
(output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in checksums.items()))
