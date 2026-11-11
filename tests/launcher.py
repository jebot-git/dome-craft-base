#!/usr/bin/python3
"""Check exact exec arguments with and without the session routing helper."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

launcher = Path(__file__).resolve().parents[1] / "packaging/artcraft-demo"
payload = ["a file with spaces", "日本語", "$(touch never-execute)", "--tray", "semi;colon"]
with tempfile.TemporaryDirectory(prefix="artcraft-launch-") as directory:
    root = Path(directory)
    record = root / "arguments.json"
    environment = {**os.environ, "PATH": directory, "ARTCRAFT_RECORD": str(record)}
    receiver = '''#!/usr/bin/python3
import json, os, sys
from pathlib import Path
Path(os.environ["ARTCRAFT_RECORD"]).write_text(json.dumps(sys.argv[1:]))
'''
    binary = root / "artcraft-base-demo"
    binary.write_text(receiver)
    binary.chmod(0o755)
    subprocess.run(["/usr/bin/python3", str(launcher), *payload], env=environment, check=True)
    assert json.loads(record.read_text()) == payload
    helper = root / "dome-link"
    helper.write_text(receiver)
    helper.chmod(0o755)
    subprocess.run(["/usr/bin/python3", str(launcher), *payload], env=environment, check=True)
    assert json.loads(record.read_text()) == ["--app-id", "org.domed.ArtCraft.BaseDemo",
                                             "--", "artcraft-base-demo", *payload]
print("Realm helper delegation and exact Unicode/metacharacter arguments passed")
