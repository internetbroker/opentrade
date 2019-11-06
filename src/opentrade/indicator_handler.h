#ifndef OPENTRADE_INDICATOR_HANDLER_H_
#define OPENTRADE_INDICATOR_HANDLER_H_

#include "algo.h"
#include "logger.h"

namespace opentrade {

namespace bp = boost::python;

struct IndicatorHandler : public Algo {
  virtual Indicator::IdType id() const = 0;
  virtual void Subscribe(Instrument* inst, bool listen = false) noexcept = 0;
  virtual void OnStart() noexcept {}
};

class IndicatorHandlerManager : public Singleton<IndicatorHandlerManager> {
 public:
  IndicatorHandler* Get(Indicator::IdType id) { return FindInMap(ihs_, id); }
  const auto& name2id() const { return name2id_; }
  bool Register(IndicatorHandler* h) {
    auto id = h->id();
    auto it = ihs_.find(id);
    if (it != ihs_.end()) {
      LOG_ERROR("Failed to register #"
                << id << " indicator of adapter " << h->name()
                << ", already registered by " << it->second->name());
      return false;
    }
    ihs_[id] = h;
    name2id_[h->name()] = id;
    return true;
  }

 private:
  // Initialized and started at AlgoManager::Run.
  std::unordered_map<size_t, IndicatorHandler*> ihs_;
  std::unordered_map<std::string, Indicator::IdType> name2id_;
  friend class Backtest;
};

}  // namespace opentrade

#endif  // OPENTRADE_INDICATOR_HANDLER_H_
