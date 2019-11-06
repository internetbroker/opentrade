#ifndef OPENTRADE_SIMULATOR_H_
#define OPENTRADE_SIMULATOR_H_
#ifdef BACKTEST

#include <boost/date_time/gregorian/gregorian.hpp>
#include <fstream>
#include <unordered_map>

#include "exchange_connectivity.h"
#include "market_data.h"
#include "order.h"
#include "security.h"

namespace opentrade {

class Simulator : public ExchangeConnectivityAdapter, public MarketDataAdapter {
 public:
  explicit Simulator(std::ostream& of) : of_(of) { connected_ = 1; }
  void Start() noexcept override {}
  void Stop() noexcept override {}
  void Reconnect() noexcept override {}
  void SubscribeSync(const opentrade::Security& sec) noexcept override {}
  std::string Place(const opentrade::Order& ord) noexcept override;
  std::string Cancel(const opentrade::Order& ord) noexcept override;
  void ResetData();
  struct OrderTuple {
    double leaves = 0;
    const Order* order = nullptr;
  };
  struct Orders {
    typedef std::multimap<double, OrderTuple> Map;
    Map buys;
    Map sells;
    std::unordered_map<Order::IdType, Map::iterator> all;
  };
  void HandleTick(const Security& sec, char type, double px, double qty,
                  double trade_hit_ratio, Orders* actives_of_sec);
  double TryFillBuy(double px, double qty, Orders* actives_of_sec);
  double TryFillSell(double px, double qty, Orders* actives_of_sec);
  auto& active_orders() { return active_orders_; }

 private:
  std::unordered_map<Security::IdType, Orders> active_orders_;
  std::ostream& of_;
  uint32_t seed_ = 0;
};

}  // namespace opentrade

#endif  // BACKTEST
#endif  // OPENTRADE_SIMULATOR_H_
