#include "database.h"

#include <postgresql/soci-postgresql.h>
#include <sqlite3/soci-sqlite3.h>
#include <boost/algorithm/string.hpp>

#include "logger.h"
#include "security.h"

namespace pt = boost::posix_time;

namespace opentrade {

time_t Database::GetTm(soci::row const& row, int index) {
  if (is_sqlite()) {
    auto tm = GetValue(row, index, kEmptyStr);
    static const pt::ptime kEpoch(boost::gregorian::date(1970, 1, 1));
    return (pt::time_from_string(tm) - kEpoch).total_seconds();
  } else {
    std::tm std_tm;
    std_tm = GetValue(row, index, std_tm);
    return MakeTime(&std_tm, "UTC");
  }
}

class SqlLog : public std::ostream, std::streambuf {
 public:
  SqlLog() : std::ostream(this) {}

  int overflow(int c) {
    if (c == '\r') return 0;
    if (c == '\n') {
      buf_[offset_] = 0;
      LOG4CXX_DEBUG(logger_, buf_);
      offset_ = 0;
    } else if (offset_ + 1 < sizeof(buf_)) {
      buf_[offset_++] = c;
    }
    return 0;
  }

 private:
  char buf_[256];
  uint32_t offset_ = 0u;
  log4cxx::LoggerPtr logger_ = opentrade::Logger::Get("sql");
};

static const char create_tables_sql[] = R"(
  create sequence if not exists exchange_id_seq start with 100;
  create table if not exists exchange(
    id int2 primary key default nextval('exchange_id_seq') not null,
    "name" varchar(50) not null,
    "mic" char(4),
    "country" char(2),
    "ib_name" varchar(50),
    "bb_name" varchar(50),
    "tz" varchar(20),
    params varchar(1000),
    odd_lot_allowed boolean,
    trade_period varchar(32), -- e.g. 09:30-15:00
    break_period varchar(32), -- e.g. 11:30-13:00
    half_day varchar(32), -- e.g. 11:30
    half_days varchar(5000), -- e.g. 20181001 20190101
    tick_size_table varchar(5000)
  );
  create unique index if not exists exchange_name_index on exchange("name");
  --pg  do $$
  --pg  begin
  --pg  if not exists(
  --pg    select 1 from exchange where id=0
  --pg  ) then
    insert into exchange(id, "name") values(0, 'default');
  --pg  end if;
  --pg  end $$;

  create sequence if not exists security_id_seq start with 10000;
  create table if not exists security(
    id int4 primary key default nextval('security_id_seq') not null,
    symbol varchar(50) not null,
    local_symbol varchar(50),
    type varchar(12) not null,
    currency char(3),
    bbgid varchar(30),
    cusip varchar(30),
    isin varchar(30),
    sedol varchar(30),
    ric varchar(30),
    rate float8,
    multiplier float8,
    tick_size float8,
    lot_size int4,
    close_price float8,
    adv20 float8,
    market_cap float8,
    sector int4,
    industry_group int4,
    industry int4,
    sub_industry int4,
    put_or_call boolean,
    opt_attribute char(1),
    maturity_date int4, -- e.g. 20201230 => 2020-12-30
    strike_price float8,
    exchange_id int2 not null references exchange(id), -- on update cascade on delete cascade,
    underlying_id int4 references security(id), -- on update cascade on delete cascade,
    params varchar(1000)
  );
  create unique index if not exists security_symbol_exchange_index on security(exchange_id, symbol);

  create sequence if not exists user_id_seq start with 100;
  create table if not exists "user"(
    id int2 primary key default nextval('user_id_seq') not null,
    "name" varchar(50) not null,
    password varchar(50) not null,
    is_admin boolean,
    is_disabled boolean,
    limits varchar(1000)
  );
  --pg  do $$
  --pg  begin
  --pg  if not exists(
  --pg    select 1 from "user" where "name" = 'admin'
  --pg  ) then
    insert into "user"(id, "name", password, is_admin)
    values(1, 'admin', 'a94a8fe5ccb19ba61c4c0873d391e987982fbbd3', true); -- passwd='test'
    insert into "user"("name", password)
    values('test', 'a94a8fe5ccb19ba61c4c0873d391e987982fbbd3'); -- passwd='test'
  --pg  end if;
  --pg  end $$;
  create unique index if not exists user_name_index on "user"("name");
  
  create sequence if not exists sub_account_id_seq start with 100;
  create table if not exists sub_account(
    id int2 primary key default nextval('sub_account_id_seq') not null,
    "name" varchar(50) not null,
    is_disabled boolean,
    limits varchar(1000)
  );
  create unique index if not exists sub_account_name_index on sub_account("name");

  create table if not exists user_sub_account_map(
    user_id int2 references "user"(id), -- on update cascade on delete cascade,
    sub_account_id int2 references sub_account(id), -- on update cascade on delete cascade,
    primary key(user_id, sub_account_id)
  );

  create sequence if not exists broker_account_id_seq start with 100;
  create table if not exists broker_account(
    id int2 primary key default nextval('broker_account_id_seq') not null,
    "name" varchar(50) not null,
    adapter varchar(50) not null,
    params varchar(1000),
    is_disabled boolean,
    limits varchar(1000)
  );
  create unique index if not exists broker_account_name_index on broker_account("name");

  create table if not exists sub_account_broker_account_map(
    sub_account_id int2 references sub_account(id), -- on update cascade on delete cascade,
    exchange_id int2 references exchange(id), -- on update cascade on delete cascade,
    broker_account_id int2 references broker_account(id), -- on update cascade on delete cascade,
    primary key(sub_account_id, exchange_id)
  );

  create sequence if not exists position_id_seq start with 100;
  create table if not exists position(
    id bigserial primary key not null,
    user_id int2 references "user"(id), -- on update cascade on delete cascade,
    sub_account_id int2 references sub_account(id), -- on update cascade on delete cascade,
    broker_account_id int2 references broker_account(id), -- on update cascade on delete cascade,
    security_id int4 references security(id), -- on update cascade on delete cascade,
    tm timestamp not null,
    qty float8 not null,
    cx_qty float8,
    avg_px float8 not null,
    realized_pnl float8 not null,
    commission float8,
    info json
  );
  create index if not exists position__index_acc_sec_tm on position(sub_account_id, security_id, tm desc);

  create table if not exists stop_book(
    security_id int4 not null references security(id), -- on update cascade on delete cascade,
    sub_account_id int2 not null references sub_account(id), -- on update cascade on delete cascade,
    primary key(security_id, sub_account_id)
  );
)";

void Database::Initialize(const std::string& url, uint8_t pool_size,
                          bool create_tables, bool alter_tables) {
#ifndef BACKTEST
  if (pool_size < 2) pool_size = 2;
#endif
  pool_ = new soci::connection_pool(pool_size);
  static SqlLog log;
  LOG_INFO("Database pool_size=" << (int)pool_size);
  LOG_INFO("Connecting to database " << url);
  if (url.find("sqlite") != std::string::npos) {
    LOG_INFO("It is sqlite");
    is_sqlite_ = true;
  }
  for (auto i = 0u; i < pool_size; ++i) {
    auto& sql = pool_->at(i);
    if (is_sqlite_) {
      sql.open(soci::sqlite3, "db=" + url + " shared_cache=true");
      sql << "PRAGMA journal_mode=WAL;";
    } else {
      sql.open(soci::postgresql, url);
    }
    sql.set_log_stream(&log);
  }
  LOG_INFO("Database connected");
  if (!create_tables) {
    try {
      *Session() << "select * from stop_book limit 1";
    } catch (const soci::soci_error& e) {
      create_tables = true;
    }
  }
  if (create_tables) {
    std::string sql = create_tables_sql;
    if (is_sqlite_) {
      boost::replace_all(sql, "int2", "integer");
      boost::replace_all(sql, "int4", "integer");
      boost::replace_all(sql, "float8", "real");
      boost::replace_all(sql, "boolean", "integer");
      boost::replace_all(sql, "json", "text");
      boost::replace_all(sql, "default nextval",
                         "autoincrement, -- default nextval");
      boost::replace_all(sql, "bigserial",
                         "integer primary key autoincrement, --");
      boost::replace_all(sql, "create sequence", "-- create sequence");
      boost::replace_all(sql, "underlying_id integer references security",
                         "underlying_id integer, -- references security");
      boost::replace_all(sql, "true", "1");
      boost::replace_all(sql, "timestamp", "text");
      // somehow, (*Session() << sql) always fail, so we execute it with sqlite3
      auto path = kStorePath / "tmp.sql";
      std::ofstream of(path.string().c_str());
      of << sql;
      of.close();
      auto cmd = "sqlite3 " + url + " < " + path.string();
      if (-1 == system(cmd.c_str())) {
        LOG_FATAL("Failed to run " << cmd);
      }
    } else {
      boost::replace_all(sql, "--pg  ", "");
      *Session() << sql;
    }
  }

  if (alter_tables) {
    auto sql = Session();
    *sql << "alter table exchange alter column trade_period type varchar(32);";
    *sql << "alter table exchange alter column break_period type varchar(32);";
    *sql << "alter table exchange alter column half_day type varchar(32);";
    try {
      *sql << "alter table security drop column name;";
      *sql << "alter table security add column ric varchar(30);";
      *sql << "alter table security add column params varchar(1000);";
      *sql << "alter table exchange drop column \"desc\";";
      *sql << "alter table exchange add column params varchar(1000);";
    } catch (...) {
    }
    try {
      *sql << "alter table position drop column \"desc\";";
      *sql << "alter table position add column info json;";
    } catch (...) {
    }
    try {
      *sql << "alter table position add column cx_qty float8;";
    } catch (...) {
    }
    try {
      *sql << "drop index position__index;";
      *sql << "create index if not exists position__index_acc_sec_tm on "
              "position(sub_account_id, security_id, tm desc);";
    } catch (...) {
    }
  }

  try {
    *Session() << "select is_disabled from sub_account limit 1";
  } catch (const soci::soci_error& e) {
    *Session() << "alter table sub_account add column is_disabled boolean;";
    *Session() << "alter table broker_account add column is_disabled boolean;";
  }

  try {
    *Session() << "select commission from position limit 1";
  } catch (const soci::soci_error& e) {
    std::string type(is_sqlite_ ? "real" : "float8");
    *Session() << "alter table position add column commission " + type + ";";
  }
}

}  // namespace opentrade
