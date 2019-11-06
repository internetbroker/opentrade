#!/usr/bin/env python3

import json
import requests
import time


def login(username, password, url):
  login = ['login', username, password]
  res = requests.post(url, data=json.dumps(login))
  res = json.loads(res.text)
  if res[1] == 'ok':
    return res[2]['sessionToken']
  else:
    raise Exception(str(res))


def run(cmd, username, password, url='http://127.0.0.1:9111/api/'):
  session_token = login(username, password, url)
  res = requests.post(url,
                      data=json.dumps(cmd),
                      headers={'session-token': session_token})
  res = json.loads(res.text)
  return res


def test():
  print(run(['securities'], 'test', 'test'))


if __name__ == '__main__':
  test()
