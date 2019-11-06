#ifndef OPENTRADE_CROSS_ENGINE_H_
#define OPENTRADE_CROSS_ENGINE_H_

#include <deque>
#include <mutex>
#include <unordered_map>

#include "algo.h"
#include "order.h"
#include "security.h"

namespace opentrade {

struct CrossOrder : public Order {
  double filled_in_market = 0;
  int count = 0;
  double leaves() { return leaves_qty - filled_in_market; }
};

struct CrossSecurity {
  std::deque<CrossOrder*> buys;
  std::deque<CrossOrder*> sells;
  std::mutex m;
  typedef std::lock_guard<std::mutex> Lock;
  void Execute(CrossOrder* ord);
  void Erase(const CrossOrder& ord);
  void Erase(Algo::IdType aid);
};

class CrossEngine : public Singleton<CrossEngine> {
 public:
  void Place(CrossOrder* ord);
  void Erase(const CrossOrder& ord) { Get(ord.sec->id)->Erase(ord); }
  void Erase(Security::IdType sid, Algo::IdType aid) { Get(sid)->Erase(aid); }
  void UpdateTrade(Confirmation::Ptr cm);

 private:
  CrossSecurity* Get(Security::IdType id) {
    Lock lock(m_);
    auto& sec = securities_[id];
    if (!sec) sec = new CrossSecurity();
    return sec;
  }

 private:
  std::unordered_map<Security::IdType, CrossSecurity*> securities_;
  std::mutex m_;
  typedef std::lock_guard<std::mutex> Lock;
  friend class Backtest;
};

}  // namespace opentrade

#endif  // OPENTRADE_CROSS_ENGINE_H_
