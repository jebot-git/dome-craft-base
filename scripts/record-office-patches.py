#!/usr/bin/python3
"""Record intentional edits in detached office worktrees; never modifies upstream originals.

Run only after reviewing/testing the diff. The regular upstream.py build verifies these digests.
"""
import hashlib
import json
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
path=root/'upstream.lock.json'
lock=json.loads(path.read_text())
for project in lock['projects']:
    source=root/'build-upstream/sources'/project['name']
    revision=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if revision!=project['revision']:raise SystemExit('Worktree revision mismatch')
    if subprocess.check_output(['git','-C',str(source),'ls-files','--others','--exclude-standard'],text=True).strip():
        raise SystemExit('Record added files explicitly before regenerating a tracked-only patch')
    files=subprocess.check_output(['git','-C',str(source),'diff','--name-only','HEAD'],text=True).splitlines()
    patch=root/'patches'/(project['name']+('-domed-native.patch' if project['name']=='photocraft' else '-domed-bitmap.patch'))
    patch.write_bytes(subprocess.check_output(['git','-C',str(source),'diff','--binary','HEAD']))
    project['patches']=[{'path':str(patch.relative_to(root)),'sha256':hashlib.sha256(patch.read_bytes()).hexdigest()}]
    if project['name']=='photocraft':project['patches'][0]['includes_upstream_fix']='https://github.com/storytold/photocraft/pull/13129'
    project['patched_files']={name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in files}
lock['common_sources']={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in lock['common_sources']}
path.write_text(json.dumps(lock,indent=2)+'\n')
