#include "sim_server.h"

#include "opentrade/algo.h"

namespace opentrade {
struct SimServerLive : public Algo, public TradeTickHook, public SimServer {
  std::string OnStart(const ParamMap& params) noexcept override {
    StartFix(*this);
    auto n = 0;
    for (auto& m : Split(config("markets"), ",; \n")) {
      auto exch = SecurityManager::Instance().GetExchange(m);
      if (!exch) {
        LOG_FATAL(name() << ": Unknown market " << m);
        continue;
      }
      for (auto& pair : exch->security_of_name) {
        auto sec = pair.second;
        auto inst = Subscribe(*sec);
        inst->HookTradeTick(this);
        ++n;
      }
    }
    LOG_INFO(name() << ": " << n << " stocks subscribed");
    return {};
  }

  void OnTrade(DataSrc::IdType src, Security::IdType id, const MarketData* md,
               time_t tm, double px, double qty) noexcept override {
    HandleTick(id, 'T', px, qty);
  }

  void OnMarketQuote(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override {
    auto q0 = md0.quote();
    auto q = md.quote();
    if (q.ask_price != q0.ask_price || q.ask_size != q0.ask_size) {
      auto qty = q.ask_size;
      if (!qty && inst.sec().type == kForexPair) qty = 1e9;
      HandleTick(inst.sec().id, 'A', q.ask_price, qty);
    }
    if (q.bid_price != q0.bid_price || q.bid_size != q0.bid_size) {
      auto qty = q.bid_size;
      if (!qty && inst.sec().type == kForexPair) qty = 1e9;
      HandleTick(inst.sec().id, 'B', q.bid_price, qty);
    }
  }
};
}  // namespace opentrade

extern "C" {
opentrade::Adapter* create() { return new opentrade::SimServerLive{}; }
}
