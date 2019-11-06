#!/usr/bin/env python3

import optparse
import pg8000
import sqlite3
import pandas as pd

types = ('STK', 'CASH', 'CMDTY', 'FUT', 'OPT', 'IND', 'FOP', 'WAR', 'BOND',
         'FUND')


def main():
  opts = optparse.OptionParser()
  opts.add_option(
      '-d',
      '--db_url',
      help='sqlite3 file path or postgres url "host,database,user,password"')
  opts.add_option('-f', '--file', help='security symbol list file')
  opts.add_option('', '--dry_run', action='store_true')
  opts = opts.parse_args()[0]

  if not opts.db_url and not opts.dry_run:
    print('Error: --db_url not give')
    return

  if not opts.file:
    print('Error: --file not give')
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
  exchanges = {}
  cursor.execute('select name, id from exchange')
  for m in cursor.fetchall():
    exchanges[m[0]] = m[1]
  cursor.execute('select id, bbgid, exchange_id, symbol from security')
  # bbgids = {} # use to identify security if symbol change
  symbols = {}
  for r in cursor.fetchall():
    # bbgids[r[1]] = r[0]
    symbols[str(r[2]) + ' ' + r[3]] = r[0]

  df = pd.read_csv(opts.file)
  cols = df.columns
  if 'exchange' not in cols or 'symbol' not in cols:
    print('exchange and symbol required in the csv')
    return
  try:
    df.exchange = df.exchange.apply(lambda x: exchanges[x])
  except KeyError as err:
    print('Unknown exchange:', err)
    return
  if hasattr(df, 'type'):
    for type in df.type:
      if type not in types:
        print('Invalid security type "%s", please choose from %s' %
              (type, str(types)))
        return
  df.rename(columns={'exchange': 'exchange_id'}, inplace=True)
  cols = df.columns
  nnew = 0
  nupdate = 0
  for index, row in df.iterrows():
    id = symbols.get(str(row['exchange_id']) + ' ' + row['symbol'])
    data = [row[c] for c in cols]
    if id is None:
      sql = 'insert into security(%s) values(%s)' % (','.join(
          ['"%s"' % c for c in cols]), ','.join(['?'] * len(cols)))
      nnew += 1
    else:
      data.append(id)
      sql = 'update security set %s where id=?' % (','.join(
          ['"%s"=?' % c for c in cols]))
      nupdate += 1
    if not is_sqlite: sql = sql.replace('?', '%s')
    cursor.execute(sql, data)

  conn.commit()
  print('%d updated, %d inserted' % (nupdate, nnew))


if __name__ == '__main__':
  main()
