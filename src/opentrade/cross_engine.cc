#include "cross_engine.h"

#include "exchange_connectivity.h"
#include "logger.h"

namespace opentrade {

void CrossEngine::Place(CrossOrder* ord) {
  assert(ord->inst);
  Get(ord->sec->id)->Execute(ord);
}

void CrossSecurity::Execute(CrossOrder* ord) {
  auto& md = ord->inst->md();
  auto price = 0.;
  auto ask = md.quote().ask_price;
  auto bid = md.quote().bid_price;
  if (ask > 0 && bid > 0) price = (ask + bid) / 2;
  if (!price) price = md.trade.close;
  if (!price) price = ord->sec->close_price;
  Lock lock(m);
  (ord->IsBuy() ? buys : sells).push_back(ord);
  if (!(buys.size() && sells.size())) return;
  if (!price) return;
  auto& ecm = ExchangeConnectivityManager::Instance();
  for (auto it_buy = buys.begin(); it_buy != buys.end() && sells.size();) {
    auto buy = *it_buy;
    if (!buy->inst->algo().is_active()) {
      it_buy = buys.erase(it_buy);
      continue;
    }
    auto a = buy->leaves();
    assert(a > 0);
    for (auto it_sell = sells.begin(); it_sell != sells.end() && a > 0;) {
      auto sell = *it_sell;
      if (!sell->inst->algo().is_active()) {
        it_sell = sells.erase(it_sell);
        continue;
      }
      if (buy->inst->algo().id() == sell->inst->algo().id()) {
        ++it_sell;
        continue;
      }
      auto b = sell->leaves();
      assert(b > 0);
      if (a >= b) {
        a -= b;
        b = 0;
      } else {
        b -= a;
        a = 0;
      }
      auto qty = sell->leaves() - b;
      assert(qty > 0);
      AlgoManager::Instance().Cancel(const_cast<Instrument*>(sell->inst));
      ecm.HandleFilled(sell, qty, price,
                       "CX-" + std::to_string(sell->id) + "-" +
                           std::to_string(sell->count++));
      if (!b) {
        it_sell = sells.erase(it_sell);
        continue;
      }
      ++it_sell;
    }
    auto qty = buy->leaves() - a;
    if (qty > 0) {
      AlgoManager::Instance().Cancel(const_cast<Instrument*>(buy->inst));
      ecm.HandleFilled(
          buy, qty, price,
          "CX-" + std::to_string(buy->id) + "-" + std::to_string(buy->count++));
    }
    if (!a) {
      it_buy = buys.erase(it_buy);
      continue;
    }
    ++it_buy;
  }
}

void CrossEngine::UpdateTrade(Confirmation::Ptr cm) {
  auto sec = Get(cm->order->sec->id);
  auto& orders = (cm->order->IsBuy() ? sec->buys : sec->sells);
  if (orders.empty()) return;
  CrossSecurity::Lock lock(sec->m);
  for (auto it = orders.begin(); it != orders.end(); ++it) {
    if ((*it)->inst == cm->order->inst) {
      (*it)->filled_in_market += cm->last_shares;
      if ((*it)->leaves() <= 0) {
        orders.erase(it);
      }
      break;
    }
  }
}

void CrossSecurity::Erase(const CrossOrder& ord) {
  Lock lock(m);
  auto& ords = (ord.IsBuy() ? buys : sells);
  auto it = std::find(ords.begin(), ords.end(), &ord);
  if (it != ords.end()) ords.erase(it);
}

void CrossSecurity::Erase(Algo::IdType aid) {
  Lock lock(m);
  for (auto i = 0; i < 2; ++i) {
    auto& ords = i ? buys : sells;
    for (auto it = ords.begin(); it != ords.end();) {
      if ((*it)->inst->algo().id() == aid)
        it = ords.erase(it);
      else
        ++it;
    }
  }
}

}  // namespace opentrade
