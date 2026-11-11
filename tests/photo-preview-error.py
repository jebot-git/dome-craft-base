#!/usr/bin/python3
"""A failed preview must not lose the live document's dirty flag or save path."""
import json,os,signal,subprocess,sys,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parents[1];build=Path(sys.argv[1] if len(sys.argv)>1 else root/'build-office').resolve()
with tempfile.TemporaryDirectory(prefix='dome-photo-preview-error-') as temporary:
    workspace=Path(temporary);report=workspace/'state.json'
    with (workspace/'stderr').open('w+') as log:
        process=subprocess.Popen(['dbus-run-session','--',str(build/'photocraft-domed-native'),'--engine',str(root/'tests/fixtures/photo-preview-error.py'),'--workspace',str(workspace),'--report',str(report)],stdout=log,stderr=log,start_new_session=True)
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                if process.poll() is not None:log.seek(0);raise AssertionError(log.read())
                try:state=json.loads(report.read_text())
                except (OSError,ValueError):time.sleep(.05);continue
                if not state.get('busy',True) and state.get('error'):
                    assert state['dirty'] is True
                    assert state['path']==str(workspace/'changed.pcraft')
                    assert state['snapshot']['document']['name']=='Changed image'
                    print('Preview failure preserves authoritative dirty state and save path');break
                time.sleep(.05)
            else:raise AssertionError('Preview failure was not reported')
        finally:
            os.killpg(process.pid,signal.SIGTERM)
            process.wait(timeout=10)
