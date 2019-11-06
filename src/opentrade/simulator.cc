#ifdef BACKTEST

#include "simulator.h"

#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "algo.h"
#include "backtest.h"
#include "logger.h"

namespace opentrade {

static boost::uuids::random_generator kUuidGen;

static inline void Async(std::function<void()> func, double seconds = 0) {
  kTimers.emplace(kTime + seconds * kMicroInSec, func);
}

inline double Simulator::TryFillBuy(double px, double qty,
                                    Orders* actives_of_sec) {
  for (auto it = actives_of_sec->buys.rbegin();
       it != actives_of_sec->buys.rend() && qty > 0 && px <= it->first;) {
    auto& tuple = it->second;
    auto n = std::fmin(qty, tuple.leaves);
    qty -= n;
    tuple.leaves -= n;
    assert(qty >= 0);
    assert(tuple.leaves >= 0);
    HandleFill(tuple.order->id, n, it->first,
               boost::uuids::to_string(kUuidGen()), 0, tuple.leaves > 0);
    auto algo_id = tuple.order->inst ? tuple.order->inst->algo().id() : 0;
    of_ << std::setprecision(15) << GetNowStr() << ','
        << tuple.order->sec->symbol << ',' << (tuple.order->IsBuy() ? 'B' : 'S')
        << ',' << n << ',' << it->first << ',' << algo_id << '\n';
    if (tuple.leaves <= 0) {
      actives_of_sec->all.erase(tuple.order->id);
      it = std::reverse_iterator(
          actives_of_sec->buys.erase(std::next(it).base()));
    } else {
      ++it;
    }
  }
  return qty;
}

inline double Simulator::TryFillSell(double px, double qty,
                                     Orders* actives_of_sec) {
  for (auto it = actives_of_sec->sells.begin();
       it != actives_of_sec->sells.end() && qty > 0 && px >= it->first;) {
    auto& tuple = it->second;
    auto n = std::fmin(qty, tuple.leaves);
    qty -= n;
    tuple.leaves -= n;
    assert(qty >= 0);
    assert(tuple.leaves >= 0);
    HandleFill(tuple.order->id, n, it->first,
               boost::uuids::to_string(kUuidGen()), 0, tuple.leaves > 0);
    auto algo_id = tuple.order->inst ? tuple.order->inst->algo().id() : 0;
    of_ << std::setprecision(15) << GetNowStr() << ','
        << tuple.order->sec->symbol << ',' << (tuple.order->IsBuy() ? 'B' : 'S')
        << ',' << n << ',' << it->first << ',' << algo_id << '\n';
    if (tuple.leaves <= 0) {
      actives_of_sec->all.erase(tuple.order->id);
      it = actives_of_sec->sells.erase(it);
    } else {
      ++it;
    }
  }
  return qty;
}

void Simulator::HandleTick(const Security& sec, char type, double px,
                           double qty, double trade_hit_ratio,
                           Orders* actives_of_sec) {
  if (!qty && sec.type == kForexPair && type != 'T') qty = 1e9;
  static bool kHasFxTrade;
  switch (type) {
    case 'T': {
      Update(sec.id, px, qty);
      if (sec.type == kForexPair) {
        if (!kHasFxTrade) kHasFxTrade = true;
        break;  // not try fill for FX trade tick
      }
      if (actives_of_sec->all.empty()) return;
      if (px > 0 && qty > 0 &&
          rand_r(&seed_) % 100 / 100. >= (1 - trade_hit_ratio)) {
        qty = TryFillBuy(px, qty, actives_of_sec);
        TryFillSell(px, qty, actives_of_sec);
      }
    } break;
    case 'A':
      Update(sec.id, px, qty, false);
      TryFillBuy(px, qty, actives_of_sec);
      if (sec.type == kForexPair && !kHasFxTrade) UpdateMidAsLastPrice(sec.id);
      break;
    case 'B':
      Update(sec.id, px, qty, true);
      TryFillSell(px, qty, actives_of_sec);
      if (sec.type == kForexPair && !kHasFxTrade) UpdateMidAsLastPrice(sec.id);
      break;
    default:
      break;
  }
}

std::string Simulator::Place(const Order& ord) noexcept {
  Async(
      [this, &ord]() {
        auto id = ord.id;
        if (!ord.sec->IsInTradePeriod()) {
          HandleNewRejected(id, "Not in trading period");
          return;
        }
        auto qty = ord.qty;
        if (qty <= 0) {
          HandleNewRejected(id, "invalid OrderQty");
          return;
        }
        if (ord.price < 0 && ord.type != kMarket) {
          HandleNewRejected(id, "invalid price");
          return;
        }
        if (ord.type == kMarket) {
          auto q = MarketDataManager::Instance().Get(*ord.sec).quote();
          auto qty_q = ord.IsBuy() ? q.ask_size : q.bid_size;
          auto px_q = ord.IsBuy() ? q.ask_price : q.bid_price;
          if (!qty_q && ord.sec->type == kForexPair) qty_q = 1e9;
          if (qty_q > 0 && px_q > 0) {
            HandleNew(id, "");
            if (qty_q > qty) qty_q = qty;
            HandleFill(id, qty_q, px_q, boost::uuids::to_string(kUuidGen()), 0,
                       qty_q != qty);
            auto algo_id = ord.inst ? ord.inst->algo().id() : 0;
            of_ << std::setprecision(15) << GetNowStr() << ','
                << ord.sec->symbol << ',' << (ord.IsBuy() ? 'B' : 'S') << ','
                << qty_q << ',' << px_q << ',' << algo_id << '\n';
            if (qty_q != qty) {
              HandleCanceled(id, id, "");
            }
            return;
          } else {
            HandleNewRejected(id, "no quote");
            return;
          }
        } else {
          HandleNew(id, "");
        }
        OrderTuple tuple{qty, &ord};
        auto& actives_of_sec = active_orders_[ord.sec->id];
        auto it = (ord.IsBuy() ? actives_of_sec.buys : actives_of_sec.sells)
                      .emplace(ord.price, tuple);
        actives_of_sec.all.emplace(id, it);
        assert(actives_of_sec.all.size() ==
               actives_of_sec.buys.size() + actives_of_sec.sells.size());
        Async([this, &ord, &actives_of_sec]() {
          auto& md = (*md_)[ord.sec->id];
          auto px = ord.IsBuy() ? md.quote().ask_price : md.quote().bid_price;
          if (!px) return;
          auto qty = ord.IsBuy() ? md.quote().ask_size : md.quote().bid_size;
          if (!qty && ord.sec->type == kForexPair) qty = 1e9;
          if (ord.IsBuy()) {
            TryFillBuy(px, qty, &actives_of_sec);
          } else {
            TryFillSell(px, qty, &actives_of_sec);
          }
        });
      },
      Backtest::Instance().latency());
  return {};
}

std::string Simulator::Cancel(const Order& ord) noexcept {
  Async(
      [this, &ord]() {
        auto& actives_of_sec = active_orders_[ord.sec->id];
        auto it = actives_of_sec.all.find(ord.orig_id);
        auto id = ord.id;
        auto orig_id = ord.orig_id;
        if (it == actives_of_sec.all.end()) {
          HandleCancelRejected(id, orig_id, "inactive");
        } else {
          HandleCanceled(id, orig_id, "");
          (ord.IsBuy() ? actives_of_sec.buys : actives_of_sec.sells)
              .erase(it->second);
          actives_of_sec.all.erase(it);
        }
      },
      Backtest::Instance().latency());
  return {};
}

void Simulator::ResetData() {
  seed_ = 0;
  for (auto& pair : *md_) {
    const_cast<Security*>(SecurityManager::Instance().Get(pair.first))
        ->close_price = pair.second.trade.close;
    pair.second.Clear();
    pair.second = MarketData{};
  }
  active_orders_.clear();
}

}  // namespace opentrade

#endif  // BACKTEST
