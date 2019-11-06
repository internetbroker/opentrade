from opentrade import *
import os
import datetime

exch = get_exchange(os.environ['MARKET'])
min_size = int(os.environ['MinSize'])
period = int(os.environ['ValidSeconds'])
agg = os.environ.get('Aggression', 'Low')
fw = open(os.environ.get('ALGOS_OUTFILE') or 'algos.txt', 'wt')


def on_start(self):
  log_info('backtest started')
  log_info('MinSize=' + str(min_size))
  log_info('ValidSeconds=' + str(period))
  log_info('Aggression=' + agg)


def on_end(self):
  log_info('backtest done')
  fw.close()


def on_start_of_day(self, date):
  now = get_datetime()
  log_info(date, 'started', now)
  fn = date.strftime('targets/%Y%m%d.txt')
  if not os.path.exists(fn):
    log_info('non-existed target file ' + fn)
    self.skip()
    return
  self.targets = []
  now -= datetime.datetime.combine(now.date(), datetime.time(0))
  now = now.seconds + now.microseconds / 1e6
  max_tm = -1
  with open(fn) as fh:
    for ln in fh:
      tm, symbol, acc, target = ln.strip().split(',')
      st = SecurityTuple()
      st.sec = exch.get_security(symbol)
      st.acc = get_account(acc)
      tm = int(tm)
      h = tm // 10000000
      m = tm % 10000000 // 100000
      tm = h * 3600 + m * 60 + tm % 100000 / 1000 - now
      if tm > max_tm: max_tm = tm
      self.set_timeout(lambda: send_algo(self, st, int(target)), tm)
  if max_tm >= 0:
    self.set_timeout(lambda: self.skip() or log_info('targets finished'),
                     max_tm + period)


def send_algo(self, st, target):
  sec = st.sec
  pos = sec.get_position(st.acc).qty
  if pos == target: return
  self.cancel_algo(sec, st.acc)
  md = sec.md
  px = 0
  ask = md.ask_price
  bid = md.bid_price
  if ask > 0 and bid > 0: px = (ask + bid) / 2
  qty = target - pos
  st.qty = abs(qty)
  st.side = OrderSide.buy if qty > 0 else OrderSide.sell
  algo = self.start_algo(
      'TWAP', {
          'Security': st,
          'MinSize': min_size,
          'Aggression': agg,
          'ValidSeconds': period
      })
  fw.write('%s,%s,%s,%d,%f,%d,%s,%f,%f,%f\n' %
           (get_datetime().strftime('%Y-%m-%d %H:%M:%S.%f'), sec.symbol, 'B'
            if qty > 0 else 'S', st.qty, px, algo, st.acc, sec.rate,
            sec.multiplier, pos))


def on_end_of_day(self, date):
  pass
