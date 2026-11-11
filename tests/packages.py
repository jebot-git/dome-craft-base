#!/usr/bin/python3
"""Verify extracted package manifests, native linkage and launcher arguments."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

packages = Path(sys.argv[1] if len(sys.argv) > 1 else "build/packages").resolve()
archive_paths = sorted(packages.glob("*-test-0.1.0-*.tar.gz"))
assert len(archive_paths) == 5, "Expected exactly five native test packages"
validated = []
with tempfile.TemporaryDirectory(prefix="artcraft-packages-") as temporary:
    root = Path(temporary)
    helper = root / "dome-link"
    helper.write_text('''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
Path(os.environ["ARTCRAFT_LAUNCH_RECORD"]).write_text(json.dumps(sys.argv[1:]))
separator = sys.argv.index("--")
os.execv(sys.argv[separator + 1], sys.argv[separator + 1:])
''')
    helper.chmod(0o755)
    record = root / "launcher.json"
    for path in archive_paths:
        with tarfile.open(path) as archive:
            archive.extractall(root, filter="data")
        stage = root / path.name.split("-test-")[0]
        stage = root / f"{stage.name}-test-0.1.0"
        manifest = json.loads((stage / "manifest.json").read_text())
        name = manifest["application"]
        for relative, digest in manifest["sha256"].items():
            assert hashlib.sha256((stage / relative).read_bytes()).hexdigest() == digest, relative
        actual = {str(p.relative_to(stage)) for p in stage.rglob("*") if p.is_file()} - {"manifest.json"}
        assert actual == set(manifest["sha256"])
        binary = stage / "bin" / f"{name}-native"
        linked = subprocess.check_output(["ldd", str(binary)], text=True)
        assert not re.search(r"lib(?:Qt[456]|gtk[-.])|not found", linked), linked
        desktop = next((stage / "share/applications").glob("*.desktop"))
        subprocess.run(["desktop-file-validate", str(desktop)], check=True)
        app_id = desktop.stem
        arguments = ["--help", "日本語 file with spaces", "$(touch never-execute)", "semi;colon"]
        result = subprocess.run([str(stage / "bin" / name), *arguments],
            env={**os.environ, "PATH": str(root), "ARTCRAFT_LAUNCH_RECORD": str(record)},
            text=True, capture_output=True, check=True)
        assert name in result.stdout
        assert json.loads(record.read_text()) == ["--app-id", app_id, "--", str(binary), *arguments]
        validated.append({"package": path.name, "files": len(actual), "launcher": "passed",
                          "manifest": "passed", "linkage": "no missing/Qt/GTK libraries"})
print(json.dumps({"packages": validated}, indent=2))
