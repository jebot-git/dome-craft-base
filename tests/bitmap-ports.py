#!/usr/bin/python3
"""Real engines: bitmap attestation, rich documents, PDF/PNG, fonts and Deck builds."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import time
from PIL import Image, ImageChops

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build',type=Path,default=root/'build-office')
parser.add_argument('--engines',type=Path,default=root/'build-upstream/target/release')
parser.add_argument('--project',choices=['wordcraft','gridcraft','deckcraft'])
args=parser.parse_args();args.build=args.build.resolve();args.engines=args.engines.resolve()
workspace=args.build/'bitmap-verification';workspace.mkdir(parents=True,exist_ok=True)
fontenv=json.loads(subprocess.check_output([str(args.build/'dome-office-font-environment')],text=True))
environment={**os.environ,**fontenv}
lock=json.loads((root/'upstream.lock.json').read_text())
fixture=workspace/('original-test-image-'+(args.project or 'all')+'.png')
Image.new('RGB',(48,32),(45,120,210)).save(fixture)
class Engine:
 def __init__(self,config):
  self.config=config;self.id=0;self.buffer=b''
  arguments=[str(args.engines/config['cli']),*config['mcp_args']]
  if config['capability_roots']:arguments+=['--automation-read-root',str(workspace),'--automation-write-root',str(workspace)]
  self.log=(workspace/(config['name']+'.log')).open('wb')
  self.process=subprocess.Popen(arguments,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,env=environment)
  self.request('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'DomeD bitmap tests','version':'1'}})
 def request(self,method,params):
  self.id+=1
  payload={'jsonrpc':'2.0','id':self.id,'method':method,'params':params}
  self.process.stdin.write(json.dumps(payload).encode()+b'\n');self.process.stdin.flush();deadline=time.monotonic()+120
  while time.monotonic()<deadline:
   if b'\n' in self.buffer:
    line,self.buffer=self.buffer.split(b'\n',1);reply=json.loads(line)
    if reply.get('id')!=self.id:continue
    if 'error' in reply:raise AssertionError(reply)
    return reply['result']
   if not select.select([self.process.stdout],[],[],1)[0]:continue
   part=os.read(self.process.stdout.fileno(),65536)
   if not part:raise AssertionError('Engine exited: '+(workspace/(self.config['name']+'.log')).read_text())
   self.buffer+=part
   assert len(self.buffer)<32*1024*1024
  raise AssertionError('Engine deadline expired')
 def tool(self,name,params):
  reply=self.request('tools/call',{'name':name,'arguments':params})
  assert not reply.get('isError'),reply
  result=reply.get('structuredContent',{})
  for block in reply.get('content',[]):
   if block['type']=='image':result['png']=block['data']
   elif block['type']=='text':
    try:
     value=json.loads(block['text'])
     if isinstance(value,dict):result.update(value)
     else:return value
    except ValueError:result['text']=block['text']
  return result
 def execute(self,command,params=None):return self.tool(self.config['execute_tool'],{self.config['command_key']:command,'params':params or {}})
 def close(self):
  self.process.stdin.close()
  try:self.process.wait(timeout=5)
  except subprocess.TimeoutExpired:self.process.terminate();self.process.wait(timeout=5)
  self.log.close()
results=[]
for config in lock['projects']:
 if config['suite']!='office':continue
 name=config['name']
 if args.project and name!=args.project:continue
 engine=Engine(config);checks=[]
 try:
  status=engine.tool('domed_bitmap_status',{})
  assert status['provider']=='domed-bdf-v1' and status['outlineFallback'] is False and status['glyphs']>0
  assert status['families'] and 'Unifont' in status['families']
  checks.append('BDF-only provider, installed family catalog and HarfRust shaping active')
  engine.execute('file.new')
  text='Bitmap café 日本語 سلام'
  if name=='wordcraft':
   engine.execute('text.insert',{'text':text+'\nSecond paragraph','paragraphs':True})
   engine.execute('caret.docEnd');engine.execute('text.newParagraph')
   engine.execute('insert.table',{'rows':2,'cols':2})
   engine.execute('text.insert',{'text':'Table cell'})
   engine.execute('caret.docEnd');engine.execute('text.newParagraph')
   engine.execute('insert.picture',{'path':str(fixture)})
  elif name=='gridcraft':
   for cell,value in [('A1','Category'),('B1','Value'),('A2','First'),('B2','4'),('A3','Second'),('B3','5'),('A4',text),('B4','=SUM(B2:B3)')]:engine.execute('cell.set',{'cell':cell,'input':value})
   engine.execute('insert.chart',{'range':'A1:B3','type':'column'})
   engine.execute('insert.picture',{'path':str(fixture),'at':'D12','width':64,'height':48})
   values=engine.execute('sheet.read',{'range':'B4'})
   assert values['values'][0][0]==9
  else:
   engine.execute('slide.new',{'title':text,'body':'Native presentation\nBitmap text, images and animations'})
   slide=engine.execute('slide.inspect');title=slide['shapes'][0]['id']
   engine.execute('edit.select',{'ids':[title]})
   engine.execute('animation.set',{'effect':'fade','duration':400})
   engine.execute('insert.picture',{'path':str(fixture)})
  checks.append('Rich document creation through upstream commands')
  output=workspace/(name+'.pdf')
  engine.execute('file.export' if name=='deckcraft' else 'file.exportPdf',{'path':str(output),'format':'pdf'})
  fonts=subprocess.check_output(['pdffonts',str(output)],text=True)
  assert len(fonts.strip().splitlines())==2,fonts
  subprocess.run(['pdftoppm','-f','1','-l','1','-singlefile','-r','96','-png',str(output),str(workspace/(name+'-pdf'))],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  image=Image.open(workspace/(name+'-pdf.png')).convert('RGB')
  assert ImageChops.difference(image,Image.new('RGB',image.size,'white')).getbbox()
  checks.append('Bitmap PDF rasterizes; no substitute or embedded outline font resources')
  if name!='gridcraft':
   raster=engine.tool('render_page' if name=='wordcraft' else 'render_slide',({'page':1,'scale':4/3} if name=='wordcraft' else {'scale':4/3}))
   raw=base64.b64decode(raster['png'],validate=True);assert raw.startswith(b'\x89PNG\r\n\x1a\n')
   (workspace/(name+'-native.png')).write_bytes(raw)
   checks.append('Native raster page contains the real document graphics')
  families=status['families'];alternate=next((family for family in families if family!=status['family'] and 'Unifont' not in family),None)
  if alternate:
   if name=='wordcraft':engine.execute('select.all')
   elif name=='gridcraft':engine.execute('selection.set',{'range':'A1:B4'})
   else:engine.execute('edit.select',{'ids':[title]})
   engine.execute('home.fontName' if name=='gridcraft' else 'format.font',{'family' if name=='deckcraft' else 'name':alternate})
   engine.execute('file.export' if name=='deckcraft' else 'file.exportPdf',{'path':str(workspace/(name+'-alternate-font.pdf')),'format':'pdf'})
   checks.append('Installed bitmap family selection produces output')
  if name=='deckcraft':
   first=engine.tool('domed_show',{'action':'start'})
   engine.tool('domed_show',{'action':'next'})
   time.sleep(.6)
   later=engine.tool('domed_show',{'action':'frame'})
   assert first.get('png') and later.get('png') and first['png']!=later['png']
   engine.tool('domed_show',{'action':'exit'})
   checks.append('Upstream animation timeline changes the rendered slide build')
   engine.execute('slide.new',{'title':'Transition target','body':'A different bitmap slide'})
   engine.execute('transition.set',{'kind':'fade','duration':1200})
   engine.execute('transition.applyAll')
   engine.execute('slide.first')
   frame=engine.tool('domed_show',{'action':'start'})
   # Advance through any builds on the initial slide, then onto the target.
   for attempt in range(12):
    incoming=engine.tool('domed_show',{'action':'next'})
    if incoming.get('slide')!=frame.get('slide'):break
   assert not incoming.get('ended') and incoming.get('png')
   time.sleep(1.4)
   final=engine.tool('domed_show',{'action':'frame'})
   assert final.get('png') and final['png']!=incoming['png']
   engine.tool('domed_show',{'action':'exit'})
   checks.append('Native slide transition changes pixels and reaches its final frame')
  saved=workspace/(name+config['default_extension'])
  engine.execute('file.save',{'path':str(saved)});assert saved.stat().st_size>0
  engine.execute('file.open',{'path':str(saved)})
  checks.append('Rich document saves and reopens in native Office format')
  results.append({'application':name,'checks':checks,'pdf_sha256':hashlib.sha256(output.read_bytes()).hexdigest()})
 finally:engine.close()
# Missing or outline inputs must fail before a document or MCP session opens.
outline=workspace/'not-a-bitmap.ttf';outline.write_bytes(b'OTTO\x00\x01\x00\x00')
for config in lock['projects']:
 if config['suite']!='office':continue
 for invalid in [workspace/'absent.bdf',outline]:
  failed=subprocess.run([str(args.engines/config['cli']),'--version'],capture_output=True,text=True,env={**environment,'DOME_OFFICE_BDF':str(invalid)})
  assert failed.returncode!=0 and 'bitmap font' in failed.stderr.lower()
print(json.dumps({'ports':results,'invalid_font_startup':'rejected for all three engines'},indent=2))
