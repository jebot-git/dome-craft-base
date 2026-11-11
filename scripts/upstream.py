#!/usr/bin/python3
"""Fetch/verify immutable Storytold sources and build their headless engines."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / "upstream.lock.json").read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("action", choices=["fetch", "verify", "build"])
parser.add_argument("projects", nargs="*", choices=[p["name"] for p in lock["projects"]])
parser.add_argument("--jobs", type=int, default=4)
args = parser.parse_args()
if args.jobs < 1 or args.jobs > 32:
    parser.error("--jobs must be between 1 and 32")
for filename,digest in lock.get("common_sources",{}).items():
    if hashlib.sha256((root/filename).read_bytes()).hexdigest()!=digest:
        raise SystemExit(f"Shared bitmap source hash mismatch: {filename}")
selected = [p for p in lock["projects"] if not args.projects or p["name"] in args.projects]
for project in selected:
    if project.get("catalogue") and hashlib.sha256((root/project["catalogue"]).read_bytes()).hexdigest()!=project["catalogue_sha256"]:
        raise SystemExit("Native command catalogue hash mismatch")
    source = root / "upstream" / project["name"]
    if args.action == "fetch":
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "init", str(source)], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(["git", "-C", str(source), "fetch", "--depth", "1", project["repository"], project["revision"]], check=True)
            subprocess.run(["git", "-C", str(source), "checkout", "--detach", "FETCH_HEAD"], check=True)
    if not (source / ".git").exists():
        raise SystemExit(f"Missing {project['name']} source: run upstream.py fetch")
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != project["revision"]:
        raise SystemExit(f"{project['name']}: source revision differs from lock; preserve changes and use a separate checkout")
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True)
    if dirty:
        raise SystemExit(f"{project['name']}: tracked source changes; refusing an unrecorded upstream build")
    for name, digest in project["license_sha256"].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"{project['name']}: license/notice hash mismatch: {name}")
    print(f"Verified {project['name']} {revision}", flush=True)
    if args.action == "build":
        build = root / "build-upstream"
        build.mkdir(exist_ok=True)
        build_source = source
        if project.get("patches"):
            # Keep the immutable source checkout clean. Build fixes live in an
            # explicit detached worktree and are verified, never silently reset.
            build_source = build / "sources" / project["name"]
            if not build_source.exists():
                build_source.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["git", "-C", str(source), "worktree", "add", "--detach",
                                str(build_source), project["revision"]], check=True)
            head = subprocess.check_output(["git", "-C", str(build_source), "rev-parse", "HEAD"], text=True).strip()
            if head != project["revision"]:
                raise SystemExit("Build worktree revision mismatch")
            changed = subprocess.check_output(["git", "-C", str(build_source), "diff", "--name-only", "HEAD"], text=True).splitlines()
            for patch in project["patches"]:
                path = root / patch["path"]
                if hashlib.sha256(path.read_bytes()).hexdigest() != patch["sha256"]:
                    raise SystemExit("Build patch digest mismatch")
                if not changed:
                    subprocess.run(["git", "-C", str(build_source), "apply", "--check", str(path)], check=True)
                    subprocess.run(["git", "-C", str(build_source), "apply", str(path)], check=True)
            changed = subprocess.check_output(["git", "-C", str(build_source), "diff", "--name-only", "HEAD"], text=True).splitlines()
            if set(changed) != set(project["patched_files"]):
                raise SystemExit("Unrecorded build-worktree modifications")
            for name, digest in project["patched_files"].items():
                if hashlib.sha256((build_source / name).read_bytes()).hexdigest() != digest:
                    raise SystemExit(f"Unexpected patched source: {name}")
        environment = {**os.environ, "CARGO_TARGET_DIR": str(build / "target"), "CARGO_BUILD_JOBS": str(args.jobs), "DOME_OFFICE_COMMON": str(root / "bitmap")}
        with (build / f"{project['name']}.log").open("w") as log:
            result = subprocess.run(["cargo", "build", "--manifest-path", str(build_source / "Cargo.toml"),
                "--locked", "--release", "-p", project["cli"], *project["cargo_args"]], env=environment,
                stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((build / f"{project['name']}.log").read_text()[-6000:])
            raise SystemExit(result.returncode)
        print(f"Built {build / 'target/release' / project['cli']}", flush=True)
