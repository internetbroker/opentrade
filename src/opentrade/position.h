#ifndef OPENTRADE_POSITION_H_
#define OPENTRADE_POSITION_H_

#include <soci.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/unordered_map.hpp>
#include <fstream>
#include <string>

#include "account.h"
#include "common.h"
#include "order.h"
#include "position_value.h"
#include "security.h"

namespace opentrade {

struct Position : public PositionValue {
  double qty = 0;
  double cx_qty = 0;  // internal crossed
  double avg_px = 0;
  double unrealized_pnl = 0;
  double realized_pnl = 0;
  double realized_pnl0 = 0;  // no rate * multiplier
  double commission = 0;
  double commission0 = 0;  // no rate * multiplier

  // intraday
  double total_bought_qty = 0;
  double total_sold_qty = 0;
  double total_outstanding_buy_qty = 0;
  double total_outstanding_sell_qty = 0;

  void HandleNew(bool is_buy, double qty, double price, double multiplier);
  void HandleTrade(bool is_buy, double qty, double price, double price0,
                   double multiplier, bool is_bust, bool is_otc, bool is_cx,
                   double cm);
  void HandleFinish(bool is_buy, double leaves_qty, double price0,
                    double multiplier);
};

struct Bod {
  double qty = 0;
  double cx_qty = 0;
  double avg_px = 0;
  double realized_pnl = 0;
  double commission = 0;
  time_t tm = 0;
  BrokerAccount::IdType broker_account_id = 0;
};

class PositionManager : public Singleton<PositionManager> {
 public:
  static void Initialize();
  auto session() { return session_; }
  void Handle(Confirmation::Ptr cm, bool offline);
  const Position& Get(const SubAccount& acc, const Security& sec) {
    return sub_positions_[std::make_pair(acc.id, sec.id)];
  }
  const Position& Get(const BrokerAccount& acc, const Security& sec) {
    return broker_positions_[std::make_pair(acc.id, sec.id)];
  }
  const Position& Get(const User& user, const Security& sec) {
    return user_positions_[std::make_pair(user.id, sec.id)];
  }
  void UpdatePnl();
  typedef tbb::concurrent_unordered_map<
      std::pair<SubAccount::IdType, Security::IdType>, Position>
      SubPositions;
  const auto& sub_positions() const { return sub_positions_; }
  const auto& broker_positions() const { return broker_positions_; }
  const auto& user_positions() const { return user_positions_; }
  typedef std::unordered_map<Security::IdType, double> Targets;
  typedef std::shared_ptr<const Targets> TargetsPtr;
  void SetTargets(const SubAccount& acc, TargetsPtr targets) {
    sub_targets_[acc.id] = targets;
  }
  auto GetTargets(const SubAccount& acc) { return sub_targets_[acc.id]; }

  struct Pnl {
    double unrealized = 0;
    double commission = 0;
    double realized = 0;
  };

 private:
  // holding the sql session exclusively for position update
  std::unique_ptr<soci::session> sql_;
  boost::unordered_map<std::pair<SubAccount::IdType, Security::IdType>, Bod>
      bods_;
  SubPositions sub_positions_;
  tbb::concurrent_unordered_map<
      std::pair<BrokerAccount::IdType, Security::IdType>, Position>
      broker_positions_;
  tbb::concurrent_unordered_map<std::pair<User::IdType, Security::IdType>,
                                Position>
      user_positions_;
  tbb::concurrent_unordered_map<BrokerAccount::IdType, TargetsPtr> sub_targets_;
  struct PnlFile : public Pnl {
    std::ofstream* of = nullptr;
  };
  tbb::concurrent_unordered_map<SubAccount::IdType, PnlFile> pnls_;
  std::string session_;
  friend class RiskMananger;
  friend class Connection;
};

}  // namespace opentrade

#endif  // OPENTRADE_POSITION_H_
