#ifndef OPENTRADE_BACKTEST_H_
#define OPENTRADE_BACKTEST_H_
#ifdef BACKTEST

#include <boost/date_time/gregorian/gregorian.hpp>
#include <cstdlib>
#include <fstream>
#include <set>

#include "python.h"
#include "security.h"

namespace opentrade {

class Simulator;

class Backtest : public Singleton<Backtest> {
 public:
  Backtest() : of_(PythonOr(std::getenv("TRADES_OUTFILE"), "trades.txt")) {}
  void Play(const boost::gregorian::date& date);
  void Start(const std::string& py, const std::string& default_tick_file);
  SubAccount* CreateSubAccount(const std::string& name,
                               const BrokerAccount* broker = nullptr);
  void End();
  void Clear();
  void Skip() { skip_ = true; }
  void AddSimulator(const std::string& fn_tmpl, const std::string& name = "");
  auto latency() { return latency_; }

 private:
  bp::object obj_;
  bp::object on_start_;
  bp::object on_start_of_day_;
  bp::object on_end_of_day_;
  bp::object on_end_;
  double latency_ = 0;  // in seconds
  double trade_hit_ratio_ = 0.5;
  std::ofstream of_;
  bool skip_;
  std::vector<std::pair<std::string, Simulator*>> simulators_;
  std::set<std::string> used_symbols_;
};

}  // namespace opentrade

#endif  // BACKTEST
#endif  // OPENTRADE_BACKTEST_H_
