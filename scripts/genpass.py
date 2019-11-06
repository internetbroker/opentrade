#!/usr/bin/env python3

import hashlib
import sys

if len(sys.argv) < 2:
  print('<passwd>')
  sys.exit(1)

h = hashlib.sha1()
h.update(sys.argv[1].encode())
print(h.hexdigest())
