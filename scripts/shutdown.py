#!/usr/bin/env python3

from test_rest import *
import sys

if len(sys.argv) < 3:
  print("usage: <username> <password> [url]")
  sys.exit(-1)

print(run(['shutdown', 3, 1], *sys.argv[1:]))
