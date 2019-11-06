#!/usr/bin/env python2

import struct
import os
import sys
import mmap

outfile = None
dump_binary = None


def main():
  if len(sys.argv) < 3:
    print('usage: convert_tick_file.py <input_tick_file> <output_tick_file>')
    return
  parse(sys.argv[1], callback, pre_callback, post_callback)


def pre_callback(symbols, symbol_type, is_text):
  global outfile, dump_binary
  dump_binary = is_text  # dump binary if original is text
  outfile = open(sys.argv[2], 'w+b')
  outfile.write('@begin ' + symbol_type)
  if dump_binary: outfile.write(' binary')
  outfile.write('\n')
  [outfile.write(x + '\n') for x in symbols]
  outfile.write('@end\n')


def callback(symbols, ms, isec, tick_type, px, size, *more):
  global outfile, dump_binary
  if dump_binary:
    raw = struct.pack('I', ms) + struct.pack(
        'H', isec) + tick_type + struct.pack('d', px) + struct.pack('I', size)
  else:
    ms = '%02d%02d%02d%03d' % (ms // 3600000, ms % 3600000 // 60000,
                               ms % 60000 // 1000, ms % 1000)
    raw = '{} {} {} {} {}\n'.format(ms, isec, tick_type, px, size)
  outfile.write(raw)


def post_callback(symbols):
  global outfile
  outfile.close()


def parse(fn, callback, pre_callback=None, post_callback=None):
  if fn == '-': infile = sys.stdin
  elif fn.endswith('xz'): infile = os.popen('xzcat ' + fn)
  else: infile = open(fn)
  line = infile.readline()
  offset = len(line)
  toks = line.strip().split()
  is_text = len(toks) == 2
  symbol_type = toks[1]
  symbols = []
  for line in infile:
    offset += len(line)
    if line.lower().startswith('@end'): break
    symbols.append(line.strip())
  if pre_callback: pre_callback(symbols, symbol_type, is_text)
  if is_text:
    for line in infile:
      toks = line.strip().split()
      hmsm = int(toks[0])
      hms = hmsm // 1000
      ms = (hms // 10000 * 3600 + hms % 10000 // 100 * 60 +
            hms % 100) * 1000 + hmsm % 1000
      sec = int(toks[1])
      t = toks[2][0]
      px = float(toks[3])
      size = int(toks[4])
      callback(symbols, ms, sec, t, px, size, *toks[5:])
  else:
    infile.close()
    infile = open(fn, 'r+b')
    mm = mmap.mmap(infile.fileno(), 0)
    while offset < len(mm):
      ms = struct.unpack('I', mm[offset:offset + 4])[0]
      offset += 4
      sec = struct.unpack('H', mm[offset:offset + 2])[0]
      offset += 2
      t = mm[offset]
      offset += 1
      px = struct.unpack('d', mm[offset:offset + 8])[0]
      offset += 8
      size = struct.unpack('I', mm[offset:offset + 4])[0]
      offset += 4
      callback(symbols, ms, sec, t, px, size)
  if post_callback: post_callback(symbols)


if __name__ == '__main__':
  main()
