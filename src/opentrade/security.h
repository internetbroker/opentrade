#ifndef OPENTRADE_SECURITY_H_
#define OPENTRADE_SECURITY_H_

#include <tbb/concurrent_unordered_map.h>
#include <string>
#include <unordered_set>

#include "common.h"
#include "utility.h"

namespace opentrade {

struct Security;

struct Exchange : public ParamsBase {
  typedef uint16_t IdType;
  IdType id = 0;
  const char* name = "";
  const char* mic = "";
  const char* bb_name = "";
  const char* ib_name = "";
  const char* country = "";
  const char* tz = "";
  bool odd_lot_allowed = false;
  int utc_time_offset = 0;
  struct TickSizeTuple {
    double lower_bound = 0;
    double upper_bound = 0;
    double value = 0;
    bool operator<(const TickSizeTuple& b) const {
      return lower_bound < b.lower_bound;
    }
  };
  typedef std::vector<TickSizeTuple> TickSizeTable;
  typedef boost::shared_ptr<const TickSizeTable> TickSizeTablePtr;
  TickSizeTablePtr tick_size_table() const {
    return tick_size_table_.load(boost::memory_order_relaxed);
  }
  double GetTickSize(double ref) const;
  int trade_start = 0;  // seconds since midnight
  int break_start = 0;
  int break_end = 0;
  int half_day = 0;
  typedef std::unordered_set<int> HalfDays;
  typedef boost::shared_ptr<const HalfDays> HalfDaysPtr;
  HalfDaysPtr half_days() const {
    return half_days_.load(boost::memory_order_relaxed);
  }

  int GetSeconds() const {  // seconds since midnight in exchange time zone
    return opentrade::GetSeconds(utc_time_offset);
  }

  int GetDate() const { return opentrade::GetDate(utc_time_offset); }

  bool IsHalfDay() const {
    auto tmp = half_days();
    return tmp && tmp->find(GetDate()) != tmp->end();
  }

  void set_trade_end(int v) { trade_end_ = v; }
  int trade_end() const { return IsHalfDay() ? half_day : trade_end_; }

  bool IsInTradePeriod() const {
    auto t = GetSeconds();
    return (break_start <= 0 || (t < break_start || t > break_end)) &&
           (trade_start <= 0 || (t > trade_start && t < trade_end()));
  }

  Security* Get(const std::string& name) const {
    return FindInMap(security_of_name, name);
  }

  tbb::concurrent_unordered_map<std::string, Security*> security_of_name;

  std::string ParseTickSizeTable(const std::string& str);
  std::string ParseHalfDays(const std::string& str);
  std::string ParsePeriod(const std::string& str, int* start = nullptr,
                          int* end = nullptr);
  std::string ParseTradePeriod(const std::string& str) {
    return ParsePeriod(str, &trade_start, &trade_end_);
  }
  std::string ParseBreakPeriod(const std::string& str) {
    return ParsePeriod(str, &break_start, &break_end);
  }
  std::string ParseHalfDay(const std::string& str);
  std::string GetTickSizeTableString() const;
  std::string GetHalfDaysString() const;
  std::string GetTradePeriodString() const;
  std::string GetBreakPeriodString() const;
  std::string GetHalfDayString() const;

 private:
  int trade_end_ = 0;
  boost::atomic_shared_ptr<const TickSizeTable> tick_size_table_;
  boost::atomic_shared_ptr<const HalfDays> half_days_;
};

// follow IB
// https://interactivebrokers.github.io/tws-api/classIBApi_1_1Contract.html
inline const std::string kStock = "STK";
inline const std::string kForexPair = "CASH";
inline const std::string kCommodity = "CMDTY";
inline const std::string kFuture = "FUT";
inline const std::string kOption = "OPT";
inline const std::string kIndex = "IND";
inline const std::string kFutureOption = "FOP";
inline const std::string kCombo = "BAG";
inline const std::string kWarrant = "WAR";
inline const std::string kBond = "BOND";

struct Security : public ParamsBase {
  typedef uint32_t IdType;
  IdType id = 0;
  const char* symbol = "";
  const char* local_symbol = "";
  const char* type = "";
  const char* currency = "";
  const char* bbgid = "";
  const char* cusip = "";
  const char* isin = "";
  const char* sedol = "";
  const char* ric = "";
  const Exchange* exchange = nullptr;
  const Security* underlying = nullptr;
  double rate = 1;  // currency rate
  double multiplier = 1;
  double tick_size = 0;
  double close_price = 0;
  double adv20 = 0;
  double market_cap = 0;
  int lot_size = 0;
  int sector = 0;
  int industry_group = 0;
  int industry = 0;
  int sub_industry = 0;
  double strike_price = 0;
  int maturity_date = 0;
  bool put_or_call = 0;
  char opt_attribute = 0;

  double CurrentPrice() const;
  double GetTickSize(double px) const {
    if (tick_size > 0) return tick_size;
    return exchange->GetTickSize(px);
  }
  bool IsInTradePeriod() const { return exchange->IsInTradePeriod(); }
#ifdef BACKTEST
  struct Adj {
    bool operator<(const Adj& b) const { return date < b.date; }
    explicit Adj(size_t a, double b = 1., double c = 1.)
        : date(a), px(b), vol(c) {}
    size_t date = 0;  // YYYYmmdd format
    double px = 1.;
    double vol = 1.;
  };
  std::vector<Adj> adjs;
#endif
};

class SecurityManager : public Singleton<SecurityManager> {
 public:
  static void Initialize();
  const char* check_sum() const { return check_sum_; }
  const Security* Get(Security::IdType id) const {
    return FindInMap(securities_, id);
  }
  const Exchange* GetExchange(Exchange::IdType id) const {
    return FindInMap(exchanges_, id);
  }
  const Exchange* GetExchange(const std::string& name) const {
    return FindInMap(exchange_of_name_, name);
  }
  const Security* Get(const std::string& exch_name,
                      const std::string& sec_name) const {
    auto exch = GetExchange(exch_name);
    return exch ? exch->Get(sec_name) : nullptr;
  }

  typedef tbb::concurrent_unordered_map<Security::IdType, Security*>
      SecurityMap;
  const SecurityMap& securities() const { return securities_; }
  void LoadFromDatabase();
  typedef tbb::concurrent_unordered_map<Exchange::IdType, Exchange*>
      ExchangeMap;
  const ExchangeMap& exchanges() const { return exchanges_; }
  auto& rates() const { return rates_; }

 protected:
  void UpdateCheckSum();

 private:
  ExchangeMap exchanges_;
  tbb::concurrent_unordered_map<std::string, Exchange*> exchange_of_name_;
  SecurityMap securities_;
  const char* check_sum_ = "";
  friend class Connection;
  std::unordered_map<std::string, double> rates_;
};

}  // namespace opentrade

#endif  // OPENTRADE_SECURITY_H_
