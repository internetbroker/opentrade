#ifndef OPENTRADE_BAR_HANDLER_H_
#define OPENTRADE_BAR_HANDLER_H_

#include "indicator_handler.h"

namespace opentrade {

static const Indicator::IdType kBar = 0;

struct Bar : public MarketData::Trade {
  time_t tm = 0;
};

template <int interval = 1, Indicator::IdType ind_id = kBar>
struct BarIndicator : public Indicator {
  static const Indicator::IdType kId = ind_id;
  Bar current;
  Bar last;
  bp::object GetPyObject() const override {
    bp::dict out;
    out["last"] = bp::ptr(&last);
    out["current"] = bp::ptr(&current);
    out["interval"] = interval;
    return out;
  }

  void Update(double px, double qty) {
    Lock lock(m_);
    current.Update(px, qty);
  }

  void Roll(time_t tm) {
    {
      Lock lock(m_);
      last = current;
      bzero(&current, sizeof(current));
    }
    last.tm = tm - 60 * interval;
  }
};

template <int interval = 1, Indicator::IdType ind_id = kBar>
class BarHandler : public IndicatorHandler, public TradeTickHook {
 public:
  typedef BarIndicator<interval, ind_id> Ind;
  explicit BarHandler(const char* name = "bar") {
    set_name(name);
    tm0_ = GetStartOfDayTime() * kMicroInSec;
  }

  void OnStart() noexcept override { StartNext(); }

  Indicator::IdType id() const override { return ind_id; }

  void Subscribe(Instrument* inst, bool listen) noexcept override {
    Async([=]() {
      auto bar = const_cast<Ind*>(inst->Get<Ind>());
      if (!bar) {
        inst->HookTradeTick(this);
        bar = new Ind{};
        bars_.push_back(bar);
        const_cast<MarketData&>(inst->md()).Set(bar);
      }
      if (listen) bar->AddListener(inst);
    });
  }

  void OnTrade(DataSrc::IdType src, Security::IdType id, const MarketData* md,
               time_t tm, double px, double qty) noexcept override {
    auto bar = const_cast<Ind*>(md->Get<Ind>());
    if (!bar) return;
    bar->Update(px, qty);
  }

  void StartNext() {
    auto now = NowInMicro();
    auto n = kMicroInMin * interval;
    auto wait = n - (NowInMicro() - tm0_) % n;
    auto ind_tm = std::round((now + wait) / kMicroInSecF);
    SetTimeout([this, ind_tm]() { OnTimer(ind_tm); }, wait / kMicroInSecF);
  }

  void OnTimer(time_t tm) {
    for (auto bar : bars_) {
      bar->Roll(tm);
      bar->Publish(ind_id);
    }
    StartNext();
  }

 private:
  uint64_t tm0_ = 0;
  std::vector<Ind*> bars_;
};

}  // namespace opentrade

#endif  // OPENTRADE_BAR_HANDLER_H_
