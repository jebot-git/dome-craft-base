#!/usr/bin/python3
# SPDX-License-Identifier: MIT
"""Small adversarial JSON-RPC process for testing the private transport."""
import json
import sys
import time

for line in sys.stdin:
    request = json.loads(line)
    if "id" not in request:
        continue
    method = request["method"]
    if method == "slow":
        time.sleep(10)
    if method == "malformed":
        print("not JSON", flush=True)
        continue
    if method == "huge":
        print('"' + "x" * (17 * 1024 * 1024) + '"', flush=True)
        continue
    if method == "notifications":
        print(json.dumps({"jsonrpc": "2.0", "method": "progress", "params": {}}), flush=True)
    result = request.get("params", {})
    if method == "tools/call":
        name = request["params"]["name"]
        result = {"isError": name == "reject", "content": [{"type": "text", "text": json.dumps({"received": request["params"]["arguments"]})}]}
    print(json.dumps({"jsonrpc": "2.0", "id": request["id"] + (1 if method == "wrong-id" else 0), "result": result}), flush=True)
