#ifndef OPENTRADE_ORDER_H_
#define OPENTRADE_ORDER_H_

#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <any>
#include <atomic>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "account.h"
#include "security.h"

namespace opentrade {

enum OrderSide : char {
  kBuy = '1',
  kSell = '2',
  kShort = '5',
};

enum OrderType : char {
  kMarket = '1',
  kLimit = '2',
  kStop = '3',
  kStopLimit = '4',
  kOTC = 'o',
  kCX = 'x',  // internal cross order
};

enum OrderStatus : char {
  kOrderStatusUnknown = 0,
  kNew = '0',
  kPartiallyFilled = '1',
  kFilled = '2',
  kDoneForDay = '3',
  kCanceled = '4',
  kReplaced = '5',
  kPendingCancel = '6',
  kStopped = '7',
  kRejected = '8',
  kSuspended = '9',
  kPendingNew = 'A',
  kCalculated = 'B',
  kExpired = 'C',
  kAcceptedForBidding = 'D',
  kPendingReplace = 'E',
  kRiskRejected = 'a',
  kUnconfirmedNew = 'b',
  kUnconfirmedCancel = 'c',
  kUnconfirmedReplace = 'd',
  kCancelRejected = 'e',
  kComment = '#',
};

enum TimeInForce : char {
  kDay = '0',
  kGoodTillCancel = '1',     // GTC
  kAtTheOpening = '2',       // OPG
  kImmediateOrCancel = '3',  // IOC
  kFillOrKill = '4',         // FOK
  kGoodTillCrossing = '5',   // GTX
  kGoodTillDate = '6',
};

enum ExecTransType : char {
  kTransNew = '0',
  kTransCancel = '1',
  kTransCorrect = '2',
  kTransStatus = '3',
};

static inline bool IsBuy(OrderSide side) { return side == kBuy; }
static inline bool IsShort(OrderSide side) { return side == kShort; }

struct Contract {
#ifdef TEST_LATENCY
  time_t tm_for_test_latency = 0;
#endif
  double qty = 0;
  double price = 0;
  double stop_price = 0;
  const Security* sec = nullptr;
  union {
    const SubAccount* sub_account = nullptr;
    const SubAccount* acc;  // alias of sub_account
  };
  // Usually you do not need to set destination,
  // We can find destination automatically from broker_account via predefined
  // sub_account_broker_account_map table. But for smart route or FX aggregator,
  // one primary exchange have many venues (ECN or LP), you need to set
  // destination manually.
  std::string destination;
  std::unordered_map<std::string, std::variant<bool, int64_t, double, char,
                                               std::string, std::any>>*
      optional = nullptr;
  OrderSide side = kBuy;
  OrderType type = kLimit;
  TimeInForce tif = kDay;

  bool IsBuy() const { return opentrade::IsBuy(side); }
  bool IsShort() const { return opentrade::IsShort(side); }
};

class Instrument;

struct Order : public Contract {
  OrderStatus status = kOrderStatusUnknown;

  // in case inst of offline order is nullptr, for frontend only
  uint32_t algo_id = 0;

  typedef uint32_t IdType;
  IdType id = 0;
  IdType orig_id = 0;
  double avg_px = 0;
  double cum_qty = 0;
  double leaves_qty = 0;
  int64_t tm = 0;
  const User* user = nullptr;
  const BrokerAccount* broker_account = nullptr;  // primary broker account
  const Instrument* inst = nullptr;

  bool IsLive() const {
    return status == kUnconfirmedNew || status == kPendingNew ||
           status == kNew || status == kSuspended || status == kPartiallyFilled;
  }
};

struct Confirmation {
  typedef std::shared_ptr<Confirmation> Ptr;
  Order* order = nullptr;
  std::string exec_id;
  std::string order_id;
  std::string text;
  OrderStatus exec_type = kOrderStatusUnknown;
  ExecTransType exec_trans_type = kTransNew;
  union {
    double last_shares = 0;
    double leaves_qty;
  };
  double last_px = 0;
  int64_t transaction_time = 0;  // utc in microseconds
  uint32_t seq = 0;
  typedef std::unordered_map<std::string, std::string> StrMap;
  typedef std::shared_ptr<StrMap> StrMapPtr;
  StrMapPtr misc;
};

class Connection;

class GlobalOrderBook : public Singleton<GlobalOrderBook> {
 public:
  static void Initialize();
  uint32_t NewOrderId() { return ++order_id_counter_; }
  bool IsDupExecId(Order::IdType id, const std::string& exec_id) {
    return !exec_ids_.emplace(id, exec_id).second;
  }
  Order* Get(Order::IdType id) {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : it->second;
  }
  void Cancel();
  void Handle(Confirmation::Ptr cm, bool offline = false);
  void LoadStore(uint32_t seq0 = 0, Connection* conn = nullptr);
  void ReadPreviousDayExecIds();
  auto GetOrders(OrderStatus status) {
    std::vector<Order*> out;
    for (auto& pair : orders_) {
      if (pair.second->status == status) out.push_back(pair.second);
    }
    return out;
  }

 private:
  void UpdateOrder(Confirmation::Ptr cm);

 private:
  tbb::concurrent_unordered_map<Order::IdType, Order*> orders_;
  std::atomic<uint32_t> order_id_counter_ = 0;
  uint32_t seq_counter_ = 0;
  tbb::concurrent_unordered_set<std::pair<Order::IdType, std::string>>
      exec_ids_;
  std::ofstream of_;
  friend class Backtest;
};

static inline bool GetOrderSide(const std::string& side_str, OrderSide* side) {
  if (!strcasecmp(side_str.c_str(), "Buy"))
    *side = kBuy;
  else if (!strcasecmp(side_str.c_str(), "Sell"))
    *side = kSell;
  else if (!strcasecmp(side_str.c_str(), "Short") ||
           !strcasecmp(side_str.c_str(), "Short Sell") ||
           !strcasecmp(side_str.c_str(), "Sell Short"))
    *side = kShort;
  else
    return false;
  return true;
}

}  // namespace opentrade

#endif  // OPENTRADE_ORDER_H_
