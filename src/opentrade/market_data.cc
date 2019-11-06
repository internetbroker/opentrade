#include "market_data.h"

#include "algo.h"
#include "logger.h"
#include "utility.h"

namespace opentrade {

inline MarketDataAdapter* MarketDataManager::GetRoute(const Security& sec,
                                                      DataSrc::IdType src) {
  auto it = routes_.find(std::make_pair(src, sec.exchange->id));
  if (it == routes_.end() && src) it = routes_.find(std::make_pair(src, 0));
  return it == routes_.end() ? default_
                             : it->second[sec.id % it->second.size()];
}

MarketDataAdapter* MarketDataManager::Subscribe(const Security& sec,
                                                DataSrc::IdType src) {
  auto adapter = GetRoute(sec, src);
  adapter->Subscribe(sec);
  return adapter;
}

const MarketData& MarketDataManager::Get(const Security& sec,
                                         DataSrc::IdType src) {
  auto adapter = GetRoute(sec, src);
  auto md = adapter->md_;
  auto it = md->find(sec.id);
  if (it == md->end()) {
    adapter->Subscribe(sec);
    return (*md)[sec.id];
  }
  return it->second;
}

const MarketData& MarketDataManager::GetLite(Security::IdType id,
                                             DataSrc::IdType src) {
  auto it = md_of_src_.find(src);
  static const MarketData kMd{};
  if (it == md_of_src_.end()) return kMd;
  return it->second[id];
}

void MarketDataManager::AddAdapter(MarketDataAdapter* adapter) {
  AdapterManager<MarketDataAdapter, kMdPrefix>::AddAdapter(adapter);

  if (!default_) default_ = adapter;
  auto src = adapter->config("src");
  if (src.size() > 4) {
    LOG_FATAL("Invalid market data src: " << src << ", maximum length is 4");
  }
  auto src_id = DataSrc::GetId(src.c_str());
  srcs_.emplace(src_id, srcs_.size());
  auto markets = adapter->config("markets");
  if (markets.empty()) markets = adapter->config("exchanges");
  adapter->md_ = &md_of_src_[src_id];
  adapter->src_ = src_id;
  for (auto& tok : Split(markets, ",;")) {
    auto orig = tok;
    boost::to_upper(tok);
    boost::algorithm::trim(tok);
    auto e = SecurityManager::Instance().GetExchange(tok);
    if (!e) {
      LOG_WARN("Unknown market name: " << orig << ", ignored");
      continue;
    }
    routes_[std::make_pair(src_id, e->id)].push_back(adapter);
  }
  if (markets.empty()) routes_[std::make_pair(src_id, 0)].push_back(adapter);
}

void MarketDataAdapter::Update(Security::IdType id, const MarketData::Quote& q,
                               uint32_t level, time_t tm) {
  if (level >= 5) return;
  auto& md = (*md_)[id];
  auto& q0 = md.depth[level];
  if (q0 == q) return;
  md.tm = tm ? tm : GetTime();
  q0 = q;
  if (level) return;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::Update(Security::IdType id, double price,
                               MarketData::Qty size, bool is_bid,
                               uint32_t level, time_t tm) {
  if (level >= 5) return;
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  auto& q = md.depth[level];
  if (is_bid) {
    q.bid_price = price;
    q.bid_size = size;
  } else {
    q.ask_price = price;
    q.ask_size = size;
  }
  if (level) return;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

static inline void UpdateTrade(MarketData* md, DataSrc::IdType src,
                               Security::IdType id, double last_price,
                               MarketData::Qty last_qty, time_t tm) {
  md->tm = tm ? tm : GetTime();
  auto& t = md->trade;
  if (last_price > 0) t.UpdatePx(last_price);
  if (last_qty > 0) t.UpdateVolume(last_qty);
  md->CheckTradeHook(src, id);
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src, id)) return;
  x.Update(src, id);
}

void MarketDataAdapter::Update(Security::IdType id, double last_price,
                               MarketData::Qty last_qty, time_t tm) {
  UpdateTrade(&(*md_)[id], src_, id, last_price, last_qty, tm);
}

void MarketDataAdapter::Update(Security::IdType id, double last_price,
                               MarketData::Volume volume, double open,
                               double high, double low, double vwap,
                               time_t tm) {
  auto& md = (*md_)[id];
  auto d = volume - md.trade.volume;
  if (d <= 0) return;
  if (md.trade.volume == 0) {
    md.tm = tm ? tm : GetTime();
    md.trade.volume = volume;
    md.trade.open = open;
    md.trade.high = high;
    md.trade.low = low;
    md.trade.close = last_price;
    md.trade.vwap = vwap;
    return;
  }
  UpdateTrade(&md, src_, id, last_price, d, tm);
}

void MarketDataAdapter::UpdateAskPrice(Security::IdType id, double v,
                                       time_t tm) {
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.depth[0].ask_price = v;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateAskSize(Security::IdType id, double v,
                                      time_t tm) {
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.depth[0].ask_size = v;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateBidPrice(Security::IdType id, double v,
                                       time_t tm) {
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.depth[0].bid_price = v;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateBidSize(Security::IdType id, double v,
                                      time_t tm) {
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.depth[0].bid_size = v;
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateLastPrice(Security::IdType id, double v,
                                        time_t tm) {
  if (v <= 0) return;
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.trade.UpdatePx(v);
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateLastSize(Security::IdType id, double v,
                                       time_t tm) {
  if (v <= 0) return;
  auto& md = (*md_)[id];
  md.tm = tm ? tm : GetTime();
  md.trade.UpdateVolume(v);
  md.CheckTradeHook(src(), id);
  auto& x = AlgoManager::Instance();
  if (!x.IsSubscribed(src_, id)) return;
  x.Update(src_, id);
}

void MarketDataAdapter::UpdateMidAsLastPrice(Security::IdType id, time_t tm) {
  auto& md = (*md_)[id];
  auto& q = md.quote();
  auto& t = md.trade;
  if (q.ask_price > q.bid_price && q.bid_price > 0) {
    auto px = (q.ask_price + q.bid_price) / 2;
    md.tm = tm ? tm : GetTime();
    t.UpdatePx(px);
    md.CheckTradeHook(src(), id);
    auto& x = AlgoManager::Instance();
    if (!x.IsSubscribed(src_, id)) return;
    x.Update(src_, id);
  }
}

void Indicator::Publish(IdType id) {
  Lock lock(m_);
  for (auto it = subs_.begin(); it != subs_.end();) {
    auto inst = *it;
    auto& algo = inst->algo();
    if (!algo.is_active()) {
      it = subs_.erase(it);
      continue;
    }
    algo.Async([&algo, id, inst]() { algo.OnIndicator(id, *inst); });
    ++it;
  }
}

}  // namespace opentrade
