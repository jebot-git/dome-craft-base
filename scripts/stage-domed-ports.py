#!/usr/bin/python3
"""Stage selected native Craft ports and a DomeD-specific office-defaults policy.

Does not install files, alter the live session, or change MIME defaults.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("stage", type=Path)
parser.add_argument("--build", type=Path, default=root / "build-office")
parser.add_argument("--engines", type=Path, default=root / "build-upstream/target/release")
parser.add_argument("--mimeapps", type=Path, help="Merge this existing DomeD preference file into the stage")
parser.add_argument("--suite", choices=["office","photo","all"], default="office")
parser.add_argument("--project",choices=["wordcraft","gridcraft","deckcraft","photocraft"],help="Stage one application")
parser.add_argument("--domed-source",type=Path,help="Canonical DomeD source used by the native build")
args = parser.parse_args()
stage = args.stage.resolve()
lock = json.loads((root / "upstream.lock.json").read_text())
suite=json.loads((root/"packaging/craft-suite.json").read_text())
if args.domed_source is None:
    cache=args.build/"CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("ARTCRAFT_DOMED_SOURCE:PATH="):args.domed_source=Path(line.split("=",1)[1]);break
if args.domed_source is None:raise SystemExit("Specify --domed-source or use a configured native build")
projects=[p for p in lock["projects"] if (args.suite=="all" or p["suite"]==args.suite or args.suite=="photo" and p["name"]=="photocraft") and (not args.project or args.project==p["name"])]
if not projects:raise SystemExit("No applications match the requested suite/project")
staged_ids={"org.domed."+p["title"]+".desktop" for p in projects}
defaults={mime:handler for mime,handler in suite["defaults"].items() if handler in staged_ids}
if stage.exists() and any(stage.iterdir()):
    parser.error("Use an empty staging directory; this prevents stale art packages entering the office suite")
if stage == Path("/") or stage == root:
    parser.error("Choose an isolated staging directory")
for project in projects:
    name = project["name"]
    for binary in [args.build / f"{name}-domed-native", args.engines / project["cli"]]:
        if not binary.is_file():
            raise SystemExit(f"Build required binary before staging: {binary}")
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", str(stage),
                    "--component", f"{name}-domed"], check=True, stdout=subprocess.DEVNULL)
    engine_dir = stage / "libexec/dome-craft"
    engine_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.engines / project["cli"], engine_dir / project["cli"])
    launcher = stage / "bin" / f"{name}-domed"
    launcher.write_text(f'''#!/usr/bin/python3
import os
from pathlib import Path
import shutil
import sys
prefix = Path(__file__).resolve().parent.parent
command = [str(prefix / "bin/{name}-domed-native"), "--engine", str(prefix / "libexec/dome-craft/{project['cli']}"), "--registry", str(prefix / "share/dome-craft/upstream.lock.json"), *sys.argv[1:]]
helper = shutil.which("dome-link")
if helper:
    os.execv(helper, [helper, "--", *command])
os.execv(command[0], command)
''')
    launcher.chmod(0o755)
    desktop_dir = stage / "share/applications"
    desktop_dir.mkdir(parents=True, exist_ok=True)
    app_id = "org.domed." + project["title"]
    categories = "Office;" if project["suite"] == "office" else "Graphics;"
    mime = "".join(mime + ";" for mime, handler in suite["defaults"].items() if handler == app_id + ".desktop")
    desktop = desktop_dir / f"{app_id}.desktop"
    desktop.write_text(f"[Desktop Entry]\nType=Application\nName={project['title']} (DomeD)\nComment=Native DomeD editor\nExec={name}-domed %f\nIcon={'applications-office' if project['suite']=='office' else 'applications-graphics'}\nTerminal=false\nCategories={categories}\nStartupWMClass={app_id}\n" + (f"MimeType={mime}\n" if mime else ""))
    subprocess.run(["desktop-file-validate", str(desktop)], check=True)
    notices = stage / "share/doc/dome-craft" / name
    notices.mkdir(parents=True, exist_ok=True)
    for filename in ["LICENSE-MIT", "LICENSE-APACHE", "NOTICE", "ATTRIBUTION.md"]:
        source = root / "upstream" / name / filename
        if source.is_file():
            shutil.copy2(source, notices / filename)
shared = stage / "share/dome-craft"
shared.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / "upstream.lock.json", shared / "upstream.lock.json")
shutil.copy2(root / "platform.lock.json", shared / "platform.lock.json")
shutil.copy2(root / "packaging/craft-suite.json", shared / "craft-suite.json")
shared_notices = stage / "share/doc/dome-craft"
shutil.copytree(root / "notices", shared_notices / "dependency-notices")
shutil.copy2(root / "UPSTREAM-PORTS.md", shared_notices / "UPSTREAM-PORTS.md")
shutil.copy2(root / "OFFICE-PORT.md", shared_notices / "OFFICE-PORT.md")
shutil.copy2(root / "PHOTO-PORT.md", shared_notices / "PHOTO-PORT.md")
shutil.copy2(root / "THIRD-PARTY.md", shared_notices / "THIRD-PARTY.md")
for filename in lock.get("common_sources",{}):
    target=shared/filename;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/filename,target)
for project in lock["projects"]:
    if project.get("catalogue"):
        target=shared/project["catalogue"];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/project["catalogue"],target)
    for patch in project.get("patches", []):
        target = shared / patch["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / patch["path"], target)
for source, name in [(root / "LICENSE", "LICENSE.Base"),
                     (args.domed_source / "LICENSE", "LICENSE.DomeD"),
                     (args.domed_source / "native/bdf2x/UNLICENSE", "UNLICENSE.bdf2x")]:
    shutil.copy2(source, shared_notices / name)
policy = configparser.ConfigParser(interpolation=None, strict=False)
policy.optionxform = str
if args.mimeapps:
    with args.mimeapps.open() as source:
        policy.read_file(source)
office_staged = any(project["suite"] == "office" for project in projects)
for section in ["Default Applications", "Added Associations"] if office_staged else []:
    if not policy.has_section(section):
        policy.add_section(section)
    for mime, handler in defaults.items():
        existing = policy[section].get(mime, "").split(";")
        retained = [item for item in existing if item and "calligra" not in item.lower() and item != handler]
        policy[section][mime] = ";".join([handler, *retained]) + ";"
    # Do not leave a dangling Calligra-only ODF default or assign an unsupported
    # ODF format to a new application.
    for mime in suite["unassigned"]:
        if not policy.has_option(section, mime):
            continue
        retained = [item for item in policy[section][mime].split(";")
                    if item and "calligra" not in item.lower()]
        if retained:
            policy[section][mime] = ";".join(retained) + ";"
        else:
            policy.remove_option(section, mime)
# This is a prepared user policy, not a globally installed mimeapps.list.
prepared = stage / "integration/dome-mimeapps.list"
if office_staged:
    prepared.parent.mkdir(parents=True, exist_ok=True)
    with prepared.open("w") as output:
        policy.write(output, space_around_delimiters=False)
manifest = {str(p.relative_to(stage)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(stage.rglob("*")) if p.is_file() and p.name != "manifest.json"}
(stage / "manifest.json").write_text(json.dumps({"suite": {name: [p["name"] for p in projects if p["suite"] == name]
    for name in ["office", "art"]}, "future": [p["name"] for p in lock["future_projects"]],
    "default_policy": "integration/dome-mimeapps.list" if office_staged else None,
    "installed": False, "readiness": "native-craft-development",
    "defaults_activated": False, "font_policy": lock["font_policy"],
    "application_font_policy": {p["name"]:p.get("font_policy",lock["font_policy"]) for p in projects},
    "excluded": [p["name"] for p in lock["excluded_projects"]], "sha256": manifest}, indent=2) + "\n")
print(stage)
