from opentrade import *

add_simulator('ticks/%Y%m%d', 'A')
add_simulator('ticks/%Y%m%d', 'B')
sec = get_exchange('US').get_security('MSFT')
sts = []


def on_start(self):
  # sec.set_adj(((20171017, 0.25, 4), (20171020, 0.5, 2)))
  log_info('backtest started')


def on_stop(self):
  log_info('backtest done')


def on_start_of_day(self, date):
  sts.clear()
  log_info(date, 'started', get_datetime())
  st = SecurityTuple()
  for src in ['A', 'B']:
    st.sec = sec
    st.src = DataSrc(src)
    st.acc = get_account(src)
    self.start_algo('AlphaExample', {'Security': st})
    sts.append(st)


def on_end_of_day(self, date):
  for st in sts:
    for sec, p in st.acc.positions:
      print(sec, p)
      print(sec.md.close)
  log_info(date, 'done', get_datetime())
