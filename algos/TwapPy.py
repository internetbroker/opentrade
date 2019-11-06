import math
from opentrade import *


def test(self):
  st = SecurityTuple()
  st.sec = get_exchange('FX').get_security('USDCAD')
  st.side = OrderSide.buy
  st.acc = get_account('SIM')
  st.qty = 10000
  return {'Security': st, 'ValidSeconds': 300, 'MinSize': 1000}


def get_param_defs():
  #  name default_value required min_value max_value precision
  return (
      ('Security', SecurityTuple(), True),
      ('Price', 0.0, False, 0, 10000000, 7),
      ('ValidSeconds', 300, True, 60),
      ('MinSize', 0, False, 0, 10000000),
      ('MaxPov', 0.0, False, 0, 1, 2),
      ('Aggression', ('Low', 'Medium', 'High', 'Highest'), True),
      ('InternalCross', ('Yes', 'No'), False),
  )


def is_buy(self):
  return self.st.side == OrderSide.buy


def on_start(self, params):
  log_debug(params)
  self.volume = 0
  self.st = st = params['Security']
  self.inst = self.subscribe(st.sec, st.src)
  seconds = params.get('ValidSeconds', 0)
  if seconds < 60:
    return 'Too short ValidSeconds, must be >= 60'
  self.begin_time = get_time()
  self.end_time = self.begin_time + seconds
  self.price = params.get('Price', 0.)
  min_size = params.get('MinSize', 0)
  lot_size = st.sec.lot_size
  if min_size <= 0 and lot_size <= 0:
    return 'MinSize required for sec without lot size'
  if min_size > 0 and lot_size > 0:
    min_size = round(min_size / lot_size) * lot_size
  self.min_size = min_size
  self.max_pov = params.get('MaxPov', 0.0)
  if self.max_pov > 1: self.max_pov = 1
  self.agg = params.get('Aggression')
  if params.get('InternalCross') == 'Yes':
    self.cross(st.qty, self.price, st.side, st.acc, self.inst)
  timer(self)
  log_debug('[' + self.name + ' ' + str(self.id) + '] started')


def on_modify(self, params):
  pass


def on_stop(self):
  log_debug('[', self.name, self.id, '] stopped')


def on_market_trade(self, inst):
  md = inst.md
  log_debug(inst.sec.symbol, 'trade:', md.open, md.high, md.low, md.close,
            md.qty, md.vwap, md.volume)
  # here is a bug, because on_market_trade is a snapshot, we do not ensure every
  # tick will call this. Please use method in its C++ version
  self.volume += md.qty


def on_market_quote(self, inst):
  md = inst.md
  log_debug(inst.sec.symbol, 'quote:', md.ask_price, md.ask_size, md.bid_price,
            md.bid_size)


def on_confirmation(self, confirmation):
  c = confirmation
  o = c.order
  log_debug('exec_id=', c.exec_id, 'order_id=', c.order_id, 'exec_type=',
            c.exec_type, 'last_shares=', c.last_shares, 'last_px=', c.last_px,
            'cum_qty=', o.cum_qty, 'avg_px=', o.avg_px, 'qty=', o.qty, 'price=',
            o.price, 'type=', o.type, 'text=', c.text)
  if c.last_shares > 0:
    qty = self.inst.total_qty
    log_debug('finished', qty, 'leaves', self.st.qty - qty, 'time elapsed',
              get_time() - self.begin_time, '/',
              self.end_time - self.begin_time)
    if qty >= self.st.qty: self.stop()


def timer(self):
  inst = self.inst
  st = self.st
  now = get_time()
  if now > self.end_time:
    self.stop()
    return
  self.set_timeout(lambda: timer(self), 1)
  if not inst.sec.is_in_trade_period: return

  md = inst.md
  bid = md.bid_price
  ask = md.ask_price
  last_px = md.close
  mid_px = 0.
  if ask > bid and bid > 0:
    mid_px = (ask + bid) / 2
    tick_size = inst.sec.get_tick_size(mid_px)
    if tick_size > 0:
      if is_buy(self):
        mid_px = math.ceil(mid_px / tick_size) * tick_size
      else:
        mid_px = math.floor(mid_px / tick_size) * tick_size

  orders = inst.active_orders
  if orders:
    for ord in orders:
      if is_buy(self):
        if ord.price < bid: self.cancel(ord)
      else:
        if ask > 0 and ord.price > ask: self.cancel(ord)
    return

  if self.volume > 0 and self.max_pov > 0:
    if inst.total_qty - inst.total_cx_qty > self.max_pov * self.volume:
      return

  ratio = min(1, (now - self.begin_time + 1) /
              (0.8 * (self.end_time - self.begin_time) + 1))
  expect = st.qty * ratio
  leaves = expect - inst.total_exposure
  if leaves <= 0: return
  total_leaves = st.qty - inst.total_exposure
  lot_size = max(1, inst.sec.lot_size)
  max_qty = total_leaves if inst.sec.exchange.odd_lot_allowed else math.floor(
      total_leaves / lot_size) * lot_size
  if max_qty <= 0: return
  would_qty = math.ceil(leaves / lot_size) * lot_size
  if would_qty < self.min_size: would_qty = self.min_size
  if would_qty > max_qty: would_qty = max_qty
  c = Contract()
  if self.agg == 'Low':
    if is_buy(self):
      if bid > 0:
        c.price = bid
      elif last_px > 0:
        c.price = last_px
      else:
        return
    else:
      if ask > 0:
        c.price = ask
      elif last_px > 0:
        c.price = last_px
      else:
        return
  elif self.agg == 'Medium':
    if mid_px > 0:
      c.price = mid_px
    else:
      return
  elif self.agg == 'High':
    if is_buy(self):
      if ask > 0:
        c.price = ask
      else:
        return
    else:
      if bid > 0:
        c.price = bid
      else:
        return
  else:
    c.type = OrderType.market
  if self.price > 0 and ((is_buy(self) and c.price > self.price) or
                         ((not is_buy(self)) and c.price < self.price)):
    return
  c.acc = st.acc
  c.qty = would_qty
  c.side = st.side
  self.place(c, inst)
