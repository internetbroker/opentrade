from opentrade import *

sec = get_exchange('US').get_security('MSFT')


def on_start(self):
  # sec.set_adj(((20171017, 0.25, 4), (20171020, 0.5, 2)))
  log_info('backtest started')


def on_stop(self):
  log_info('backtest done')


def on_start_of_day(self, date):
  log_info(date, 'started', get_datetime())
  st = SecurityTuple()
  st.sec = sec
  self.start_algo('AlphaExample', {'Security': st})
  self.acc = st.acc


def on_end_of_day(self, date):
  for sec, p in self.acc.positions:
    print(sec, p)
    print(sec.md.close)
  log_info(date, 'done', get_datetime())
