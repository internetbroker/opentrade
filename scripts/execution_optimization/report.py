#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
from collections import defaultdict


def main():
  accs = defaultdict(int)
  for ln in open(os.environ.get('ALGOS_OUTFILE', 'algos.txt')):
    if ln.startswith('#'): continue
    fds = ln.strip().split(',')
    if len(fds) > 6: accs[fds[6]] += 1
  accs = [(v, k) for k, v in accs.items()]
  accs.sort()
  if accs:
    for acc in accs:
      report_acc(acc[1])
  else:
    report_acc('')


def report_acc(acc):
  x = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))
  costs = defaultdict(
      lambda: defaultdict(lambda: defaultdict(lambda: [0, 0, 0])))
  attrs = {}
  for ln in open(os.environ.get('ALGOS_OUTFILE', 'algos.txt')):
    if ln.startswith('#'): continue
    fds = ln.strip().split(',')
    tm, symbol, side, qty, px, algo_id, acc2, rate, multiplier = fds[:9]
    if acc and acc2 != acc: continue
    x[symbol][side][-1] += float(qty)
    costs[symbol][side][algo_id][-1] = float(px)
    attrs[symbol] = (float(rate), float(multiplier))
  trades = defaultdict(lambda: defaultdict(list))
  rpnl = defaultdict(lambda: [0, 0, 0])
  for ln in open(os.environ.get('TRADES_OUTFILE', 'trades.txt')):
    fds = ln.strip().split(',')
    tm, symbol, side, qty, px, algo_id = fds[:6]
    if algo_id not in costs[symbol][side]: continue
    y = x[symbol][side]
    px = float(px)
    qty = float(qty)
    y[1] = (y[1] * y[0] + qty * px) / (y[0] + qty)
    y[0] += qty
    p = costs[symbol][side][algo_id]
    p[1] = (p[1] * p[0] + qty * px) / (p[0] + qty)
    p[0] += qty
    trades[symbol][side].append([qty, px])
    pnl = rpnl[symbol]
    qty *= 1 if side == 'B' else -1
    assert (px > 0)
    if pnl[0] * qty >= 0:
      pnl[1] = (pnl[0] * pnl[1] + qty * px) / (pnl[0] + qty)
    else:
      n = qty
      px0 = pnl[1]
      if abs(qty) > abs(pnl[0]):
        pnl[1] = px
        n = -pnl[0]
      tmp = n * (px0 - px)
      pnl[-1] += tmp
    pnl[0] += qty
  if acc:
    print('acc: ' + acc)
  print(''.join(['-'] * 210))
  print(
      '%12s  |  %s %-12s %-12s %-8s %-8s %-8s %-8s  |   %s %-12s %-12s %-8s %-8s %-8s %-8s   |   %-8s %-8s %-8s %-8s %-8s %-8s'
      % ('symbol', 'side', 'qty', 'avg_px', 'fr', 'avg_cost', 'min_cost',
         'max_cost', 'side', 'qty', 'avg_px', 'fr', 'avg_cost', 'min_cost',
         'max_cost', 'fr', 'avg_cost', 'min_cost', 'max_cost', 'rpnl', 'tvr'))
  print(''.join(['-'] * 210))
  total_cost = []
  total_fr = []
  total_qty = []
  total_pnl = 0
  total_tvr = 0
  for symbol, i in x.items():
    spx = i['S'][1]
    bpx = i['B'][1]
    # bqty = i['B'][-1]
    # sqty = i['S'][-1]
    pnl = 0
    bfilled = i['B'][0]
    sfilled = i['S'][0]
    tvr = bfilled * bpx + sfilled * spx
    for attr in attrs[symbol]:
      if attr > 0: tvr *= attr
    total_tvr += tvr
    qty = min(bfilled, sfilled)
    if qty > 0 and bfilled != sfilled:
      buys = trades[symbol]['B']
      sells = trades[symbol]['S']
      assert (bfilled == sum([_[0] for _ in buys]))
      assert (sfilled == sum([_[0] for _ in sells]))
      delta = bfilled - qty
      while delta > 0:
        if delta >= buys[-1][0]:
          delta -= buys[-1][0]
          buys.pop()
        else:
          buys[-1][0] -= delta
          break
      delta = sfilled - qty
      while delta > 0:
        if delta >= sells[-1][0]:
          delta -= sells[-1][0]
          sells.pop()
        else:
          sells[-1][0] -= delta
          break
      bpx = sum([_[0] * _[1] for _ in buys]) / qty
      spx = sum([_[0] * _[1] for _ in sells]) / qty
      assert (qty == sum([_[0] for _ in buys]))
      assert (qty == sum([_[0] for _ in sells]))
    pnl = rpnl[symbol][-1]
    for attr in attrs[symbol]:
      if attr > 0: pnl *= attr
    total_pnl += pnl

    bpx = bpx or ''
    if bpx: bpx = '%-12.6f' % bpx
    bfr = i['B'][0] / (i['B'][-1] or 1) or ''
    if bfr: bfr = '%-8.4f' % bfr
    spx = spx or ''
    if spx: spx = '%-12.6f' % spx
    sfr = i['S'][0] / (i['S'][-1] or 1) or ''
    if sfr: sfr = '%-8.4f' % sfr
    tmp1 = [(x[1] - x[-1]) / x[-1]
            for x in costs[symbol]['B'].values()
            if x[-1] > 0 and x[1] > 0]
    bcost = ['', '', '']
    if tmp1:
      bcost = [
          '%-8.2f' % (sum(tmp1) / len(tmp1) * 1e4),
          '%-8.2f' % (min(tmp1) * 1e4),
          '%-8.2f' % (max(tmp1) * 1e4)
      ]
    tmp2 = [
        -(x[1] - x[-1]) / x[-1]
        for x in costs[symbol]['S'].values()
        if x[-1] > 0 and x[1] > 0
    ]
    scost = ['', '', '']
    if tmp2:
      scost = [
          '%-8.2f' % (sum(tmp2) / len(tmp2) * 1e4),
          '%-8.2f' % (min(tmp2) * 1e4),
          '%-8.2f' % (max(tmp2) * 1e4)
      ]
    tmp = tmp1 + tmp2
    cost = ['', '', '']
    fr = ((i['B'][0] + i['S'][0]) / ((i['B'][-1] + i['S'][-1]) or 1)) or ''
    if tmp:
      cost = [
          '%-8.2f' % (sum(tmp) / len(tmp) * 1e4),
          '%-8.2f' % (min(tmp) * 1e4),
          '%-8.2f' % (max(tmp) * 1e4)
      ]
      total_cost.append(sum(tmp) / len(tmp) * 1e4)
      total_fr.append(fr)
      total_qty.append(bfilled + sfilled)
      fr = '%-8.4f' % fr
    print(
        '%12s  |  %s    %-12s %-12s %-8s %-8s %-8s %-8s  |   %s    %-12s %-12s %-8s %-8s %-8s %-8s   |   %-8s %-8s %-8s %-8s %-8s %-8s'
        % (symbol, 'B',
           str(bfilled or ''), bpx, bfr, bcost[0], bcost[1], bcost[2], 'S',
           str(sfilled or ''), spx, sfr, scost[0], scost[1], scost[2], fr,
           cost[0], cost[1], cost[2], '%.0f' % pnl, '%f' % (tvr / 1e6)))
  print(''.join(['-'] * 210))
  if total_cost:
    cost_fr = sum([total_cost[i] * total_fr[i] for i in range(len(total_cost))
                  ]) / sum(total_fr)
    cost_qty = sum([
        total_cost[i] * total_qty[i] for i in range(len(total_cost))
    ]) / sum(total_qty)
    print(
        'cost: %f        fr: %f        sum(cost*fr)/sum(fr): %f        sum(cost*filled_qty)/sum(filled_ty): %f        rpnl: %f        tvr: %f'
        % (sum(total_cost) / len(total_cost), sum(total_fr) / len(total_fr),
           cost_qty, cost_fr, total_pnl, total_tvr / 1e6))
    print('\n')


if __name__ == '__main__':
  main()
