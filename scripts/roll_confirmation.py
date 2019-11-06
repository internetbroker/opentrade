#!/usr/bin/env python2
'''
roll yesterday's unfinished orders to today.
scripts/roll_confirmation.py archives/`date -d '1 days ago' "+%Y%m%d"`/store/confirmations store/confirmations
'''

import datetime
import time
import os
from parse_confirmation import *
from collections import defaultdict

confirmations = []
orders = {}
now = time.time()
one_day = 24 * 3600 * 1000
exec_ids = defaultdict(list)


def check_confirmation(seq, raw, exec_type, acc, id, *args):
  if exec_type == kUnconfirmedNew:
    tm, algo_id, qty = args[:3]
    x = now - int(tm) / 1e6
    if x > one_day:
      log('too old orders skipped', x / one_day, 'days ago')
      return
    orders[id] = float(qty)
    confirmations.append((id, raw))
  elif exec_type == kNew:
    confirmations.append((id, raw))
  elif exec_type in (kRiskRejected, kCanceled, kRejected, kExpired, kCalculated,
                     kDoneForDay):
    if id in orders: del orders[id]
  elif exec_type in (kPartiallyFilled, kFilled):
    tm, last_shares, last_px, exec_trans_type, exec_id = args[:5]
    exec_ids[id].append(exec_id)
    n = float(last_shares)
    if id in orders:
      if exec_trans_type == kTransCancel: orders[id] += n
      elif exec_trans_type == kTransNew:
        orders[id] -= n
        if orders[id] <= 1e-6:
          del orders[id]


def main():
  src = sys.argv[1]
  dest = sys.argv[2]
  if os.path.exists(dest):
    log(dest, 'already exists, skip rolling', src)
    return
  if not os.path.exists(src):
    log(src, 'not exists, skip rolling')
    return
  fh = open(dest, 'wb')
  rolls = []
  parse(src, check_confirmation)
  rolls = [(id, raw) for id, raw in confirmations if id in orders]
  log(len(orders), 'orders rolled')
  seq = 0
  for id, raw in rolls:
    seq += 1
    if raw[6] == kUnconfirmedNew:
      n = struct.unpack('H', raw[4:6])[0]
      body = raw[9:9 + n]
      body = body.split(' ')
      # modify qty
      qty = orders[id]
      body[3] = str(qty)
      body = ' '.join(body)
      exec_type = raw[6]
      acc = raw[7:9]
      raw = struct.pack('I', seq) + struct.pack(
          'H', len(body)) + exec_type + acc + body + '\0\n'
      for exec_id in exec_ids[id]:
        body = 'exec_id {} {}'.format(id, exec_id)
        seq += 1
        raw += struct.pack('I', seq) + struct.pack(
            'H', len(body)) + '#' + acc + body + '\0\n'
    else:
      raw = struct.pack('I', seq) + raw[4:]
    fh.write(raw)
  fh.close()


def log(*args):
  args = (datetime.datetime.now(),) + args
  print(' '.join([str(x) for x in args]))


if __name__ == '__main__':
  main()
