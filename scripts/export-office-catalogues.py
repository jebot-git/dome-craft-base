#!/usr/bin/python3
"""Export pinned command metadata for native menu construction before engine startup."""
import hashlib,json,os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
lockfile=root/'upstream.lock.json';lock=json.loads(lockfile.read_text())
env={**os.environ,**json.loads(subprocess.check_output([str(root/'build-office/dome-office-font-environment')],text=True))}
for project in lock['projects']:
 request=lambda i,method,params:json.dumps({'jsonrpc':'2.0','id':i,'method':method,'params':params})+'\n'
 data=request(1,'initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'DomeD catalogues','version':'1'}})
 data+=request(2,'tools/call',{'name':project['commands_tool'],'arguments':{}})
 result=subprocess.run([str(root/'build-upstream/target/release'/project['cli']),*project['mcp_args']],input=data,text=True,capture_output=True,env=env,check=True,timeout=30)
 reply=next(json.loads(line)['result'] for line in result.stdout.splitlines() if json.loads(line).get('id')==2)
 commands=json.loads(next(item['text'] for item in reply['content'] if item['type']=='text'))
 if isinstance(commands,dict):commands=commands['commands']
 for command in commands:
  for key in ['enabled','enabled_known']:command.pop(key,None)
 path=root/'ports/catalogues'/(project['name']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(commands,indent=2,ensure_ascii=False)+'\n')
 project['catalogue']=str(path.relative_to(root));project['catalogue_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
 print(project['name'],len(commands))
lockfile.write_text(json.dumps(lock,indent=2)+'\n')
