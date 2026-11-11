#!/usr/bin/python3
"""Small MCP fixture: valid unsaved document, deliberately failed preview."""
import json
from pathlib import Path
import sys
root=Path(sys.argv[sys.argv.index('--automation-read-root')+1])
path=str(root/'changed.pcraft')
for line in sys.stdin:
    request=json.loads(line)
    if 'id' not in request:continue
    method=request['method'];params=request.get('params',{})
    if method=='initialize':result={'protocolVersion':'2024-11-05','capabilities':{'tools':{}},'serverInfo':{'name':'preview-failure-fixture','version':'1'}}
    elif method=='tools/list':result={'tools':[{'name':name,'inputSchema':{'type':'object'}} for name in ['command_run','command_list','doc_inspect','render_preview','doc_new','session_list']]}
    else:
        name=params['name']
        if name=='render_preview':
            result={'isError':True,'content':[{'type':'text','text':'preview deliberately unavailable'}]}
            print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}),flush=True);continue
        if name=='doc_new':value={'document':0}
        elif name=='doc_inspect':value={'width':8,'height':8,'revision':2,'name':'Changed image','path':path,'activeLayer':2,'layers':[{'id':2,'name':'Layer','kind':'Pixel','opacity':1,'visible':True}],'history':['Open','Edit']}
        elif name=='session_list':value={'active':0,'documents':[{'index':0,'name':'Changed image','path':path,'dirty':True,'revision':2,'width':8,'height':8}]}
        elif name=='command_list':value=[{'id':'file.save','label':'Save','enabled':True,'params':'{}'}]
        else:value={}
        result={'content':[{'type':'text','text':json.dumps(value)}]}
    print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}),flush=True)
