#!/usr/bin/env python3

import optparse
import pg8000
import sqlite3
from forex_python.converter import CurrencyRates
c = CurrencyRates()


def main():
  opts = optparse.OptionParser()
  opts.add_option(
      '-d',
      '--db_url',
      help='sqlite3 file path or postgres url "host,database,user,password"')
  opts.add_option('', '--dry_run', action='store_true')
  opts = opts.parse_args()[0]

  rates = c.get_rates('USD')
  cmds = [
      "update security set rate={} where currency='{}';".format(1 / rate, cur)
      for cur, rate in rates.items()
  ]
  if opts.dry_run:
    for cmd in cmds:
      print(cmd)
    return

  if not opts.db_url:
    print('Error: --db_url not give')
    return

  is_sqlite = False
  if opts.db_url.endswith('sqlite3'):
    is_sqlite = True
    conn = sqlite3.connect(opts.db_url)
  else:
    host, database, user, password = opts.db_url.split(',')
    conn = pg8000.connect(host=host,
                          database=database,
                          user=user,
                          password=password)

  cursor = conn.cursor()
  [cursor.execute(cmd) for cmd in cmds]
  conn.commit()


if __name__ == '__main__':
  main()
