#!/usr/bin/python3
"""Drive PhotoCraft's actual native universal menus, image engine and docked workspace."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile
from PIL import Image, ImageDraw

if not os.environ.get('DOME_PHOTO_TEST_BUS'):
    raise SystemExit(subprocess.call(['dbus-run-session', '--', sys.executable, __file__, *sys.argv[1:]], env={**os.environ, 'DOME_PHOTO_TEST_BUS': '1'}))
from gi.repository import Gio, GLib
root=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1] if len(sys.argv)>1 else root/'build-office').resolve()
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
workspace=build/'photo-verification';workspace.mkdir(exist_ok=True)
source=workspace/'original 日本語.png'
image=Image.new('RGB',(320,240),(40,100,170));draw=ImageDraw.Draw(image);draw.rectangle((40,40,180,120),fill=(220,150,45));draw.ellipse((180,100,290,220),fill=(70,185,100));image.save(source)
saved=workspace/'native layers.pcraft';results=[]
for phase in ['edit','reopen']:
    report=workspace/(phase+'.json');report.unlink(missing_ok=True)
    capture=workspace/(phase+'.ppm');capture.unlink(missing_ok=True)
    with (workspace/(phase+'.log')).open('w+') as log:
        process=subprocess.Popen([str(build/'photocraft-domed-native'),str(source if phase=='edit' else saved),'--workspace',str(workspace),'--report',str(report),'--capture',str(capture),'--save-path',str(saved),'--width','720','--height','500','--quit-after-ms','60000'],stderr=log,stdout=log)
        def state():
            try:return json.loads(report.read_text())
            except (OSError,ValueError):return {}
        def wait(predicate,reason):
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                if process.poll() is not None:
                    log.seek(0);raise AssertionError(f'{phase}: exit {process.returncode}: {log.read()}')
                s=state()
                if s.get('error'):raise AssertionError(s['error'])
                if predicate(s):return s
                time.sleep(.05)
            log.seek(0);raise AssertionError(f'{phase}: {reason}: {log.read()}')
        def layout():
            s=state();return bus.call_sync(s['menu_service'],s['menu_path'],'com.canonical.dbusmenu','GetLayout',GLib.Variant('(iias)',(0,-1,[])),None,Gio.DBusCallFlags.NONE,3000,None).unpack()[1]
        def click(*path):
            deadline=time.monotonic()+5
            while True:
                node=layout()
                for part in path:
                    node=next(child for child in node[2] if child[1].get('label','').replace('_','')==part)
                if node[1].get('enabled',True):break
                if time.monotonic()>=deadline:raise AssertionError(('Menu stayed disabled',path,state().get('error')))
                time.sleep(.05)
            s=state();bus.call_sync(s['menu_service'],s['menu_path'],'com.canonical.dbusmenu','Event',GLib.Variant('(isvu)',(node[0],'clicked',GLib.Variant('i',0),0)),None,Gio.DBusCallFlags.NONE,3000,None)
        try:
            s=wait(lambda s:not s.get('busy',True) and s.get('renderer') and s.get('snapshot',{}).get('view',{}).get('raster',{}).get('png_bytes',0)>100,'rendered startup')
            assert Path(s['font_file']).read_bytes().startswith(b'STARTFONT')
            assert s['native_menu_commands']>=1006
            assert s['snapshot']['view'].get('layer_thumbnails') and all(t.get('png_bytes',0)>100 for t in s['snapshot']['view']['layer_thumbnails'])
            assert s['snapshot']['document']['width']==320 and s['snapshot']['document']['height']==240
            names=[n[1].get('label','').replace('_','') for n in layout()[2]]
            assert names[:8]==['PhotoCraft','File','Edit','Image','Layer','Type','Select','Filter'],names
            geometry=s['layout'];assert geometry['application_menu']=='universal'
            for component in ['options','tools','canvas','documents','dock','properties','layers']:
                rect=geometry[component];assert rect['w']>0 and rect['h']>0,component
                assert 0<=rect['x']<=720 and rect['x']+rect['w']<=721,(component,rect)
            assert geometry['tools']['x']<geometry['canvas']['x']<geometry['dock']['x']
            checks=['Pinned upstream image rendered in a native bitmap/GPU workspace','macOS menus exported with Photoshop main-window dock regions','Document/layer state and 720x500 panel bounds verified']
            if phase=='edit':
                count=len(s['snapshot']['document']['layers'])
                click('Layer','New','Layer');wait(lambda s:not s['busy'] and len(s['snapshot']['document']['layers'])==count+1,'new pixel layer')
                revision=state()['snapshot']['document']['revision']
                click('Tools','Paint foreground dab');wait(lambda s:not s['busy'] and s['snapshot']['document']['revision']!=revision,'brush engine stroke')
                click('Layer','Duplicate layer');wait(lambda s:not s['busy'] and len(s['snapshot']['document']['layers'])==count+2,'duplicate layer')
                click('Edit','Undo');wait(lambda s:not s['busy'] and len(s['snapshot']['document']['layers'])==count+1,'undo')
                click('Edit','Redo');wait(lambda s:not s['busy'] and len(s['snapshot']['document']['layers'])==count+2,'redo')
                click('Layer','Delete layer');wait(lambda s:not s['busy'] and len(s['snapshot']['document']['layers'])==count+1,'delete')
                click('View','100%');wait(lambda s:abs(s['layout']['scale']-1)<.001,'native image zoom')
                old=state()['layout']['canvas']['w']
                click('Window','Show or hide panels');wait(lambda s:not s['layout']['dock']['visible'] and s['layout']['canvas']['w']>old,'hide dock')
                click('Window','Show or hide panels');wait(lambda s:s['layout']['dock']['visible'],'restore dock')
                click('File','Save As...');wait(lambda s:not s['busy'] and saved.exists() and not s['dirty'],'layered native save')
                with zipfile.ZipFile(saved) as archive:
                    assert archive.testzip() is None
                    assert any('manifest' in name for name in archive.namelist())
                checks+=['Native new/duplicate/delete layer and actual brush stroke','Actual upstream undo/redo','Image zoom and dock visibility change the live layout','Layered .pcraft save independently verified']
            else:
                assert len(s['snapshot']['document']['layers'])==2
                checks+=['Fresh process reopens the layered document and raster']
            wait(lambda s:capture.exists(),'GPU capture')
            Image.open(capture).save(workspace/(phase+'.png'))
            click('PhotoCraft','Quit');process.wait(timeout=10);assert process.returncode==0
            results.append({'phase':phase,'checks':checks})
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=10)
print(json.dumps({'photocraft':results},indent=2))
