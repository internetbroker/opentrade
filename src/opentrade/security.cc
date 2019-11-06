#include "security.h"

#include <boost/make_shared.hpp>
#include <cstring>
#include <unordered_map>

#include "database.h"
#include "logger.h"
#include "market_data.h"

namespace opentrade {

std::string Exchange::ParseTickSizeTable(const std::string& str) {
  if (str.size()) {
    auto tmp = boost::make_shared<TickSizeTable>();
    for (auto& str : Split(str, ",;\n")) {
      double low, up, value;
      if (sscanf(str.c_str(), "%lf %lf %lf", &low, &up, &value) == 3) {
        tmp->push_back({low, up, value});
      } else {
        return "Invalid tick size table format, expect '<low_price> <up_price> "
               "<value>[,;<new line>]...'";
      }
    }
    if (!tmp->empty()) {
      tmp->shrink_to_fit();
      std::sort(tmp->begin(), tmp->end());
      tick_size_table_.store(tmp, boost::memory_order_release);
    }
  }
  return {};
}

std::string Exchange::ParsePeriod(const std::string& str, int* start,
                                  int* end) {
  if (str.empty()) {
    if (start) *start = 0;
    if (end) *end = 0;
    return {};
  }
  if (atoi(str.c_str()) > 10000) {  // for back-compactible, will remove
    auto period = atoi(str.c_str());
    auto a = period / 10000;
    auto b = period % 10000;
    if (start) *start = (a / 100) * 3600 + (a % 100) * 60;
    if (end) *end = ((b / 100) * 3600 + (b % 100) * 60);
    return {};
  }
  int a, b, c, d;
  if (sscanf(str.c_str(), "%d:%d-%d:%d", &a, &b, &c, &d) != 4) {
    return "Invalid trade period, expect 'HH:MM-HH:MM'";
  }
  if (start) *start = a * 3600 + b * 60;
  if (end) *end = c * 3600 + d * 60;
  return {};
}

std::string Exchange::ParseHalfDay(const std::string& str) {
  if (str.empty()) {
    half_day = 0;
    return {};
  }
  if (atoi(str.c_str()) > 1000) {  // for back-compactible, will remove
    auto tmp = atoi(str.c_str());
    half_day = (tmp / 100) * 3600 + (tmp % 100) * 60;
    return {};
  }
  int a, b;
  if (sscanf(str.c_str(), "%d:%d", &a, &b) != 4) {
    return "Invalid half day end time, expect 'HH:MM'";
  }
  half_day = a * 3600 + b * 60;
  return {};
}

std::string Exchange::GetTickSizeTableString() const {
  std::stringstream out;
  auto tmp = tick_size_table();
  if (!tmp) return {};
  out << std::setprecision(15);
  for (auto i = 0u; i < tmp->size(); ++i) {
    auto t = (*tmp)[i];
    if (i > 0) out << "\n";
    out << t.lower_bound << ' ' << t.upper_bound << ' ' << t.value;
  }
  return out.str();
}

std::string Exchange::ParseHalfDays(const std::string& str) {
  if (str.size()) {
    auto tmp = boost::make_shared<HalfDays>();
    for (auto& f : Split(str, ",;\n")) {
      auto i = atoi(f.c_str());
      if (i > 0) {
        tmp->insert(i);
      }
    }
    if (tmp->empty()) {
      return "Invalid half days format, expect '<YYYmmdd>[,;<new line>]...'";
    }
    half_days_.store(tmp, boost::memory_order_release);
  }
  return {};
}

std::string Exchange::GetHalfDaysString() const {
  std::stringstream out;
  auto tmp = half_days();
  if (!tmp) return {};
  out << std::setprecision(15);
  auto it = tmp->begin();
  while (it != tmp->end()) {
    if (it != tmp->begin()) out << "\n";
    out << *it;
    ++it;
  }
  return out.str();
}

std::string Exchange::GetTradePeriodString() const {
  if (!trade_start) return {};
  char buf[256];
  snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", trade_start / 3600,
           trade_start % 3600 / 60, trade_end_ / 3600, trade_end_ % 3600 / 60);
  return buf;
}

std::string Exchange::GetBreakPeriodString() const {
  if (!break_start) return {};
  char buf[256];
  snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", break_start / 3600,
           break_start % 3600 / 60, break_end / 3600, break_end % 3600 / 60);
  return buf;
}

std::string Exchange::GetHalfDayString() const {
  if (!half_day) return {};

  char buf[256];
  snprintf(buf, sizeof(buf), "%02d:%02d", half_day / 3600,
           half_day % 3600 / 60);
  return buf;
}

void SecurityManager::Initialize() {
  auto& self = Instance();
  self.LoadFromDatabase();
}

void SecurityManager::LoadFromDatabase() {
  auto sql = Database::Session();

  auto query = R"(
    select id, "name", mic, params, country, ib_name, bb_name, tz, tick_size_table, 
    odd_lot_allowed, trade_period, break_period, half_day, half_days from exchange
  )";
  soci::rowset<soci::row> st = sql->prepare << query;
  for (auto it = st.begin(); it != st.end(); ++it) {
    auto i = 0;
    auto id = Database::GetValue(*it, i++, 0);
    auto eit = exchanges_.find(id);
    auto e = (exchanges_.end() == eit) ? new Exchange() : eit->second;
    e->id = id;
    e->name = Database::GetValue(*it, i++, "");
    e->mic = Database::GetValue(*it, i++, "");
    e->SetParams(Database::GetValue(*it, i++, kEmptyStr));
    e->country = Database::GetValue(*it, i++, "");
    e->ib_name = Database::GetValue(*it, i++, "");
    e->bb_name = Database::GetValue(*it, i++, "");
    e->tz = Database::GetValue(*it, i++, "");
    if (*e->tz) e->utc_time_offset = GetUtcTimeOffset(e->tz);
    e->ParseTickSizeTable(Database::GetValue(*it, i++, kEmptyStr));
    e->odd_lot_allowed = Database::GetValue(*it, i++, 0);
    e->ParseTradePeriod(Database::GetValue(*it, i++, kEmptyStr));
    e->ParseBreakPeriod(Database::GetValue(*it, i++, kEmptyStr));
    e->ParseHalfDay(Database::GetValue(*it, i++, kEmptyStr));
    e->ParseHalfDays(Database::GetValue(*it, i++, kEmptyStr));

    std::atomic_thread_fence(std::memory_order_release);
    exchanges_.emplace(e->id, e);
    exchange_of_name_.emplace(e->name, e);
  }

  std::unordered_map<Security*, Security::IdType> underlying_map;
  query = R"(
    select id, symbol, local_symbol, type, currency, exchange_id, underlying_id, rate,
           multiplier, tick_size, lot_size, close_price, strike_price, maturity_date,
           put_or_call, opt_attribute, bbgid, cusip, isin, sedol, ric,
           adv20, market_cap, sector, industry_group, industry, sub_industry, params
    from security
  )";
  st = sql->prepare << query;
  for (auto it = st.begin(); it != st.end(); ++it) {
    auto i = 0;
    auto id = Database::GetValue(*it, i++, 0);
    auto sit = securities_.find(id);
    auto s = (securities_.end() == sit) ? new Security() : sit->second;
    s->id = id;
    s->symbol = Database::GetValue(*it, i++, "");
    s->local_symbol = Database::GetValue(*it, i++, "");
    s->type = Database::GetValue(*it, i++, "");
    s->currency = Database::GetValue(*it, i++, "");
    auto exchange_id = Database::GetValue(*it, i++, 0);
    auto ex_it = exchanges_.find(exchange_id);
    if (ex_it != exchanges_.end()) {
      s->exchange = ex_it->second;
      ex_it->second->security_of_name.emplace(s->symbol, s);
    }
    auto underlying_id = Database::GetValue(*it, i++, Security::IdType());
    if (underlying_id > 0) {
      underlying_map[s] = underlying_id;
    }
    s->rate = Database::GetValue(*it, i++, s->rate);
    if (s->rate > 0 && *s->currency) rates_[s->currency] = s->rate;
    if (s->rate <= 0) s->rate = 1;
    s->multiplier = Database::GetValue(*it, i++, s->multiplier);
    if (s->multiplier <= 0) s->multiplier = 1;
    s->tick_size = Database::GetValue(*it, i++, s->tick_size);
    s->lot_size = Database::GetValue(*it, i++, s->lot_size);
    s->close_price = Database::GetValue(*it, i++, s->close_price);
    s->strike_price = Database::GetValue(*it, i++, s->strike_price);
    s->maturity_date = Database::GetValue(*it, i++, s->maturity_date);
    s->put_or_call = Database::GetValue(*it, i++, 0);
    auto opt_attribute = Database::GetValue(*it, i++, kEmptyStr);
    if (!opt_attribute.empty()) s->opt_attribute = opt_attribute.at(0);
    s->bbgid = Database::GetValue(*it, i++, "");
    s->cusip = Database::GetValue(*it, i++, "");
    s->isin = Database::GetValue(*it, i++, "");
    s->sedol = Database::GetValue(*it, i++, "");
    s->ric = Database::GetValue(*it, i++, "");
    s->adv20 = Database::GetValue(*it, i++, 0.);
    s->market_cap = Database::GetValue(*it, i++, 0.);
    s->sector = Database::GetValue(*it, i++, 0);
    s->industry_group = Database::GetValue(*it, i++, 0);
    s->industry = Database::GetValue(*it, i++, 0);
    s->sub_industry = Database::GetValue(*it, i++, 0);
    s->SetParams(Database::GetValue(*it, i++, kEmptyStr));
    std::atomic_thread_fence(std::memory_order_release);
    securities_.emplace(s->id, s);
  }
  LOG_INFO(securities_.size() << " securities loaded");
  for (auto& pair : underlying_map) {
    auto it = securities_.find(pair.second);
    if (it != securities_.end()) pair.first->underlying = it->second;
  }
  UpdateCheckSum();
}

void SecurityManager::UpdateCheckSum() {
  extern std::string sha1(const std::string& str);
  std::stringstream ss;
  for (auto& pair : securities_) {
    auto s = pair.second;
    ss << pair.first << s->symbol << s->exchange->name << s->type << s->lot_size
       << s->multiplier << s->currency;
  }
  check_sum_ = strdup(sha1(ss.str()).c_str());
}

double Security::CurrentPrice() const {
  auto px = MarketDataManager::Instance().Get(*this).trade.close;
  return px > 0 ? px : close_price;
}

double Exchange::GetTickSize(double ref) const {
  auto table = tick_size_table();
  if (!table) return 0;
  TickSizeTuple t{ref};
  auto it = std::lower_bound(table->begin(), table->end(), t);
  if (it == table->end()) return 0;
  return it->value;
}

}  // namespace opentrade
