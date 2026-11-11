#!/usr/bin/python3
"""Write a thin, pinned native-build entry point into one downstream Craft fork."""
import argparse
import json
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('project',choices=['wordcraft','gridcraft','deckcraft','photocraft'])
parser.add_argument('repository',type=Path)
parser.add_argument('--base-revision',required=True)
parser.add_argument('--base-url',default='https://github.com/jebot-git/dome-craft-base.git')
args=parser.parse_args()
if not re.fullmatch('[0-9a-f]{40}',args.base_revision):parser.error('Use an immutable shared-base commit SHA')
lock=json.loads((root/'upstream.lock.json').read_text());config=next(c for c in lock['projects'] if c['name']==args.project)
repo=args.repository.resolve();folder=repo/'native/domed';folder.mkdir(parents=True,exist_ok=True)
(folder/'base.lock.json').write_text(json.dumps({'application':args.project,'repository':args.base_url,'revision':args.base_revision,'upstream_revision':config['revision'],'cargo_args':config['cargo_args']},indent=2)+'\n')
(folder/'.gitignore').write_text('/_build/\n/_deps/\n__pycache__/\n')
(folder/'build.py').write_text('''#!/usr/bin/python3
"""Build this fork's engine and its DomeD frontend from an immutable shared base."""
import argparse
import json
import os
from pathlib import Path
import subprocess

native=Path(__file__).resolve().parent
repository=native.parents[1]
lock=json.loads((native/'base.lock.json').read_text())
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--domed-source',type=Path,required=True,help='Canonical DomeD checkout with native GPU/menu/BDF headers')
parser.add_argument('--toolkit',type=Path,help='Installed DomeD-patched FLTK 1.4.5 CMake prefix')
parser.add_argument('--base-source',type=Path,help='Already checked-out shared base at the locked revision')
parser.add_argument('--engine-dir',type=Path,help='Reuse prebuilt headless engines from this directory')
parser.add_argument('--build',type=Path,default=native/'_build')
parser.add_argument('--jobs',type=int,default=4)
args=parser.parse_args()
if not 1<=args.jobs<=32:parser.error('jobs must be 1–32')
args.domed_source=args.domed_source.resolve()
if not (args.domed_source/'native/gpu-window.h').is_file():parser.error('domed-source lacks the canonical native integration')
def run(command,**kwargs):subprocess.run([str(value) for value in command],check=True,**kwargs)
base=args.base_source.resolve() if args.base_source else native/'_deps/dome-craft-base'
if not base.exists():
    base.parent.mkdir(parents=True,exist_ok=True)
    run(['git','clone','--filter=blob:none','--no-checkout',lock['repository'],base])
    run(['git','-C',base,'fetch','--depth','1','origin',lock['revision']])
    run(['git','-C',base,'checkout','--detach',lock['revision']])
actual=subprocess.check_output(['git','-C',str(base),'rev-parse','HEAD'],text=True).strip()
if actual!=lock['revision']:raise SystemExit('Shared base revision differs from base.lock.json; use a separate checkout')
if subprocess.check_output(['git','-C',str(base),'status','--porcelain','--untracked-files=no'],text=True).strip():raise SystemExit('Shared base has tracked changes; refusing an unrecorded build')
project=lock['application'];build=args.build.resolve();build.mkdir(parents=True,exist_ok=True)
engines=args.engine_dir.resolve() if args.engine_dir else build/'engine-target/release'
if not args.engine_dir:
    environment={**os.environ,'DOME_OFFICE_COMMON':str(base/'bitmap'),'CARGO_TARGET_DIR':str(build/'engine-target'),'CARGO_BUILD_JOBS':str(args.jobs)}
    run(['cargo','build','--manifest-path',repository/'Cargo.toml','--locked','--release','-p',project+'-cli',*lock['cargo_args']],env=environment)
if not (engines/(project+'-cli')).is_file():raise SystemExit('Required engine is missing: '+str(engines/(project+'-cli')))
command=['cmake','-S',base,'-B',build/'native','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF','-DDOME_OFFICE_BUILD_LEGACY_FIXTURES=OFF','-DARTCRAFT_DOMED_SOURCE='+str(args.domed_source),'-DDOME_CRAFT_ENGINE_DIR='+str(engines)]
if args.toolkit:command+=['-DCMAKE_PREFIX_PATH='+str(args.toolkit.resolve())]
run(command)
run(['cmake','--build',build/'native','--target',project+'-domed-native','-j',args.jobs])
# The local launcher forwards argument boundaries and delegates realm selection.
launcher=build/(project+'-domed')
program=[str(build/'native'/(project+'-domed-native')),'--engine',str(engines/(project+'-cli')),'--registry',str(base/'upstream.lock.json')]
launcher.write_text(chr(10).join(['#!/usr/bin/python3','import os,shutil,sys','command='+repr(program)+'+sys.argv[1:]','helper=shutil.which("dome-link")','if helper:os.execv(helper,[helper,"--",*command])','os.execv(command[0],command)'])+chr(10))
launcher.chmod(0o755)
print(launcher)
''')
(folder/'build.py').chmod(0o755)
(folder/'README.md').write_text(f'''# {config['title']} for DomeD

This branch ports the pinned Storytold engine to the shared DomeD native base.
The upstream application and document engine remain authoritative. The native
frontend uses DomeD's FLTK/GPU window, bitmap UI fonts, universal menu and
current desktop/XR realm. The original upstream frontend is retained separately.

The shared frontend is fetched from `{args.base_url}` at the immutable revision
in `base.lock.json`; it is not copied into each application fork. The office
engines use that same base's BDF shaping/rendering provider. PhotoCraft retains
its upstream image/type compositor with bitmap native UI text.

```sh
python3 native/domed/build.py --domed-source ../DomeD \\
  --toolkit /opt/fltk-vr/toolkit
native/domed/_build/{args.project}-domed --workspace /path/to/documents
```

Requirements: Rust, Python, CMake, C++20, canonical DomeD source, its patched FLTK
1.4.5 toolkit, installed Unicode BDF fonts, OpenGL/EGL, GIO, Wayland, X11 metadata,
Fontconfig, FreeType, zlib, nlohmann-json and Poppler's `pdftoppm` for office page
previews. A healthy desktop session bus supplies the universal-menu connection.
No new compositor, font assets, Qt/GTK frontend or ArtCraft AI app is included.

`--base-source` reuses an existing clean shared-base checkout at the locked SHA.
`--engine-dir` reuses separately built engines for frontend development. Build
outputs live under `native/domed/_build` and are excluded from source control.

See the shared base's [office implementation](https://github.com/jebot-git/dome-craft-base/blob/main/OFFICE-PORT.md)
and [PhotoCraft implementation](https://github.com/jebot-git/dome-craft-base/blob/main/PHOTO-PORT.md)
for validation and remaining development limits. Bitmap office PDFs currently
lack searchable text; bespoke-panel and physical XR parity are not certified.
''')
readme=repo/'README.md'
marker='> **DomeD native port:**'
if readme.is_file() and marker not in readme.read_text():
    readme.write_text('> **DomeD native port:** see [native/domed](native/domed/README.md) for the shared bitmap/GPU frontend and build instructions. Original upstream documentation follows.\n\n'+readme.read_text())
print(folder)
