#!/usr/bin/env python3

from test_rest import *
import json
import sys

if len(sys.argv) < 4:
  print("usage: <cmd_json> <username> <password> [url]")
  sys.exit(-1)

print(run(json.loads(sys.argv[1]), *sys.argv[2:]))
