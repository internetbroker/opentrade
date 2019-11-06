#ifndef ALGOS_TWAP_TWAP_H_
#define ALGOS_TWAP_TWAP_H_

#include "opentrade/algo.h"
#include "opentrade/security.h"

namespace opentrade {

enum Aggression {
  kAggLow,
  kAggMedium,
  kAggHigh,
  kAggHighest,
};

class TWAP : public Algo {
 public:
  std::string OnStart(const ParamMap& params) noexcept override;
  void OnModify(const ParamMap& params) noexcept override;
  void OnStop() noexcept override;
  void OnMarketTrade(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override;
  void OnMarketQuote(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override;
  void OnConfirmation(const Confirmation& cm) noexcept override;
  const ParamDefs& GetParamDefs() noexcept override;
  void Timer();
  virtual Instrument* Subscribe();
  virtual const MarketData& md() { return inst_->md(); }
  virtual void Place(Contract* c) { Algo::Place(*c, inst_); }
  virtual double GetLeaves() noexcept;
  double RoundPrice(double px) {
    auto tick_size = inst_->sec().GetTickSize(px);
    if (tick_size > 0) {
      if (IsBuy(st_.side))
        px = std::floor(px / tick_size) * tick_size;
      else
        px = std::ceil(px / tick_size) * tick_size;
    }
    if (px > 100) {
      px = Round6(px);
    } else {
      px = Round8(px);
    }
    return px;
  }

 protected:
  Instrument* inst_ = nullptr;
  SecurityTuple st_;
  double price_ = 0;
  time_t begin_time_ = 0;
  time_t end_time_ = 0;
  int min_size_ = 0;
  int max_floor_ = 0;
  double max_pov_ = 0;
  double initial_volume_ = 0;
  Aggression agg_ = kAggLow;
};

}  // namespace opentrade

#endif  // ALGOS_TWAP_TWAP_H_
