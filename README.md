# OpenTrade

![OpenTrade Logo](https://github.com/opentradesolutions/opentrade/blob/master/web/img/ot.png)

OpenTrade is an open source OEMS, and algorithmic trading platform, designed for simplicity, flexibility and performance. 

[**Demo**](http://demo.opentradesolutions.com)

---

# Features:
* Strictly follows [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
* Multi-level account functionality
* Super simple API interfaces for market data adapter (C++), exchange connectivity (C++) and execution/alpha algo (C++ and Python)
* **REST** and web socket interface
* Multi-source market data support, e.g., different FX pricing sources
* Pre-trade risk limits on multi-level accounts
* Post-trade risk integrated with [OpenRisk](https://github.com/opentradesolutions/openrisk)
* Edit and test **Python** algo online
* **Backtest**
* [Multi-theme web frontend](http://demo.opentradesolutions.com)
* Fully thread-safe design, everything can be modified during runtime, e.g., reload symbol list, modify tick size table, lot-size, exchange timezone and trading/break period etc.
* Built-in execution simulator
* Simple configuration
* **Internal cross**
* **Execution Optimization Framework**
* Order Aggregation
* Customized indicator extension
* **Smart Route and FX aggregation**
* Support both PostgreSQL and Sqlite3

---

[![Algo Editor](https://github.com/opentradesolutions/opentrade/blob/master/imgs/algo-editor.png)](https://raw.githubusercontent.com/opentradesolutions/opentrade/master/imgs/algo-editor.png)

[![OpenRisk](https://github.com/opentradesolutions/openrisk/blob/master/image.png)](https://raw.githubusercontent.com/opentradesolutions/openrisk/master/image.png)

---

# Steps to run on Ubuntu 18.04 or later
* **Compile**
  * Prepare dev environment.
  ```bash
  sudo apt update \
  && sudo apt install -y \
    g++  \
    make \
    cmake \
    clang-format \
    clang \
    python \
    python3-dev \
    vim \
    exuberant-ctags \
    git \
    wget \
    libssl-dev \
    libboost-program-options-dev \
    libboost-system-dev \
    libboost-date-time-dev \
    libboost-filesystem-dev \
    libboost-iostreams-dev \
    libboost-python-dev \
    libsoci-dev \
    libpq-dev \
    sqlite3 \
    libsqlite3-dev \
    libquickfix-dev \
    libtbb-dev \
    liblog4cxx-dev
  ```

  * Build
  ```bash
  git clone https://github.com/opentradesolutions/opentrade
  cd opentrade
  make debug
  ```
  
* **Setup database**
   * Sqlite3
   ```bash
   wget https://github.com/opentradesolutions/data/raw/master/test.sqlite3
   ```

   * **Or** PostgreSQL
   ```bash
   sudo apt remove --purge postgres*
   sudo apt autoremove
   sudo apt install -y postgresql-10 || sudo apt install -y postgresql-11
   sudo apt install -y postgresql-contrib postgresql-client
   # add data to database as user 'postgres'
   sudo su postgres;
   cd;
   wget https://github.com/opentradesolutions/data/raw/master/opentrade-pg_dumpall.sql
   psql -f opentrade-pg_dumpall.sql
   exit # become yourself again
   ```
 
* **Run opentrade**
   * Download tick data files
   ```bash
   cd opentrade
   wget https://raw.githubusercontent.com/opentradesolutions/data/master/bbgids.txt
   wget https://github.com/opentradesolutions/data/raw/master/ticks.txt.xz.part1
   wget https://github.com/opentradesolutions/data/raw/master/ticks.txt.xz.part2
   cat ticks.txt.xz.part1 ticks.txt.xz.part2 > ticks.txt.xz
   xz -d ticks.txt.xz
   ```
   * Run
   ```Bash
   # please modify opentrade.conf to use postgres if setup database with PostgreSQL
   cp opentrade.conf-example opentrade.conf
   ./opentrade
   ```
   
* **Open Web UI**
   ```
   # username/password: test/test
   http://localhost:9111
   ```
   
# CentOS 8

 Please checkout [install_centos.sh](https://github.com/opentradesolutions/opentrade/blob/master/install_centos.sh)

# Internal Latency
  ```
  make test-latency
  ```
   
# Backtest
  * Only BBO support currently, full orderbook support will come soon
  * It is up to you to generate report
  ```
  make args=-j backtest-debug
  wget -O ticks.tgz https://www.dropbox.com/s/maikrn2qbz8hxba/ticks.tgz?dl=1; tar xzf ticks.tgz
  ./build/backtest-debug/opentrade/opentrade -b scripts/backtest.py -t ticks/%Y%m%d -s 20170701 -e 20181115
  ```
  
# Execution Optimization Example
  ```
  make args=-j backtest-release
  cd scripts/execution_optimization
  wget https://raw.githubusercontent.com/opentradesolutions/data/master/targets.zip; unzip targets.zip;
  wget https://github.com/opentradesolutions/data/raw/master/test.sqlite3
  ./run
  ./sim_summary.py rpt*
  ```

|    params          |    avg cost        |    avg fr          |    total pnl       |    total tvr       |
|--------------------|--------------------|--------------------|--------------------|--------------------|
|    High-0150       |    0.9378          |    1.0000          |    -0.0089         |    125.6283        |
|    High-0300       |    0.9297          |    1.0000          |    -0.0084         |    125.6273        |
|    High-0450       |    1.0504          |    1.0000          |    -0.0093         |    125.6262        |
|    High-0600       |    1.1687          |    1.0000          |    -0.0101         |    125.6252        |
|    High-0900       |    1.2693          |    1.0000          |    -0.0111         |    125.6255        |
|    High-1200       |    1.2615          |    1.0000          |    -0.0119         |    125.6264        |
|    High-1500       |    1.2867          |    1.0000          |    -0.0123         |    125.6280        |
|    High-1800       |    1.3281          |    1.0000          |    -0.0132         |    125.6296        |
|    High-2100       |    1.3886          |    1.0000          |    -0.0141         |    125.6324        |
|    High-2400       |    1.4285          |    1.0000          |    -0.0151         |    125.6340        |
|    High-2700       |    1.4940          |    1.0000          |    -0.0158         |    125.6348        |
|    High-3000       |    1.7564          |    0.9974          |    -0.0173         |    125.6373        |
|    Low-0150        |    -0.0681         |    0.5145          |    -0.0497         |    96.6273         |
|    Low-0300        |    0.1620          |    0.7019          |    -0.0244         |    108.1107        |
|    Low-0450        |    -0.1739         |    0.8185          |    -0.0295         |    117.1855        |
|    Low-0600        |    0.3163          |    0.8836          |    -0.0148         |    118.0378        |
|    Low-0900        |    0.3646          |    0.8884          |    -0.0177         |    120.5907        |
|    Low-1200        |    0.7914          |    0.9347          |    -0.0131         |    125.6282        |
|    Low-1500        |    0.8468          |    0.9427          |    -0.0106         |    125.6280        |
|    Low-1800        |    1.0057          |    0.9932          |    -0.0130         |    125.3894        |
|    Low-2100        |    1.2167          |    0.9862          |    -0.0161         |    125.6313        |
|    Low-2400        |    1.2075          |    1.0000          |    -0.0138         |    125.6348        |
|    Low-2700        |    1.3235          |    1.0000          |    -0.0150         |    125.6363        |
|    Low-3000        |    1.5444          |    0.9956          |    -0.0162         |    125.6386        |
|    Medium-0150     |    0.3631          |    0.5238          |    -0.0541         |    98.7558         |
|    Medium-0300     |    0.3999          |    0.7333          |    -0.0190         |    109.3046        |
|    Medium-0450     |    0.5549          |    0.8820          |    -0.0141         |    117.3461        |
|    Medium-0600     |    0.7546          |    0.8941          |    -0.0219         |    124.6843        |
|    Medium-0900     |    0.7669          |    0.8994          |    -0.0221         |    125.6307        |
|    Medium-1200     |    1.0957          |    0.9366          |    -0.0161         |    125.6295        |
|    Medium-1500     |    1.1907          |    0.9427          |    -0.0135         |    125.6285        |
|    Medium-1800     |    1.3788          |    0.9932          |    -0.0162         |    125.3895        |
|    Medium-2100     |    1.4976          |    0.9982          |    -0.0166         |    125.6344        |
|    Medium-2400     |    1.5517          |    1.0000          |    -0.0169         |    125.6354        |
|    Medium-2700     |    1.6557          |    1.0000          |    -0.0180         |    125.6368        |
|    Medium-3000     |    1.8620          |    0.9956          |    -0.0192         |    125.6392        |

# The other OS system
  we prepared [Dockfile-dev](https://raw.githubusercontent.com/opentradesolutions/opentrade/master/Dockfile-dev) for you.
