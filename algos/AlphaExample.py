import math
from opentrade import *


def get_param_defs():
  #  name default_value required min_value max_value precision
  return (('Security', SecurityTuple(), True),)


def on_start(self, params):
  self.st = st = params['Security']
  self.inst = self.subscribe(st.sec, st.src)
  # self.inst.subscribe('bar', True)
  self.vwap15 = [[0, 0], [], 15]
  self.vwap30 = [[0, 0], [], 30]
  self.set_timeout(lambda: self.stop(),
                   st.sec.exchange.trade_end - st.sec.exchange.seconds)
  self.set_timeout(
      lambda: close_pos(self),
      st.sec.exchange.trade_end - st.sec.exchange.seconds - 60)
  self.end = False


def on_indicator(self, ind, inst):
  log_debug(str(ind['last']))

def close_pos(self):
  self.end = True
  place(self, 0)


def on_stop(self):
  log_debug('[', self.name, self.id, '] stopped')


def on_confirmation(self, c):
  pass


def update_vwap(vwap, tm, px, qty):
  if px <= 0 or qty <= 0: return
  value, hist, interval = vwap
  while hist and tm - hist[0][-1] >= interval:
    qty0 = value[1]
    value[1] -= hist[0][1]
    assert (value[1] >= 0)
    if value[1] > 0:
      value[0] = (value[0] * qty0 - hist[0][0] * hist[0][1]) / value[1]
    else:
      value[0] = 0
    hist = hist[1:]
  vwap[1] = hist
  if not hist:
    hist.append([px, qty, tm])
    value[0] = px
    value[1] = qty
    return
  qty0 = value[1]
  value[1] += qty
  value[0] = (value[0] * qty0 + px * qty) / value[1]
  if tm == hist[-1][-1]:
    qty0 = hist[-1][1]
    hist[-1][1] += qty
    hist[-1][0] = (hist[-1][0] * qty0 + px * qty) / hist[-1][1]
  else:
    hist.append([px, qty, tm])


def on_market_trade(self, inst):
  if not self.is_active or self.end: return
  m = int(inst.sec.exchange.seconds) / 60
  close = inst.md.close
  qty = inst.md.qty
  if not close or not qty: return
  update_vwap(self.vwap15, m, close, qty)
  update_vwap(self.vwap30, m, close, qty)
  pos = self.st.sec.get_position(self.st.acc).qty
  if self.vwap15[0][0] > self.vwap30[0][0]:
    if pos < 0:
      place(self, 0)
    elif not pos:
      place(self, 100, OrderSide.buy)
  elif self.vwap15[0][0] < self.vwap30[0][0]:
    if pos > 0:
      place(self, 0)
    elif not pos:
      place(self, 100, OrderSide.short)


def place(self, qty, side=None):
  if not self.inst.sec.is_in_trade_period: return
  c = Contract()
  c.acc = self.st.acc
  if qty:
    c.side = side
    c.qty = qty
  else:
    pos = self.st.sec.get_position(c.acc).qty
    if pos > 0:
      c.qty = pos
      c.side = OrderSide.sell
    elif pos < 0:
      c.qty = -pos
      c.side = OrderSide.buy
    else:
      return
  c.type = OrderType.market
  self.place(c, self.inst)
