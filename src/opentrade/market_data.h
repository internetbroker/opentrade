#ifndef OPENTRADE_MARKET_DATA_H_
#define OPENTRADE_MARKET_DATA_H_

#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <atomic>
#include <boost/python.hpp>
#include <boost/unordered_map.hpp>
#include <shared_mutex>
#include <string>

#include "adapter.h"
#include "security.h"

namespace opentrade {

struct DataSrc {
  typedef uint32_t IdType;

  IdType value = 0;
  DataSrc() : value(0) {}
  explicit DataSrc(IdType v) : value(v) {}
  explicit DataSrc(const std::string& src) : value(GetId(src.c_str())) {}
  operator IdType() const { return value; }
  const char* str() const { return GetStr(value); }
  const IdType operator()() { return value; }
  operator IdType() { return value; }
  DataSrc& operator=(IdType v) {
    value = v;
    return *this;
  }
  DataSrc& operator=(const std::string& v) {
    value = GetId(v.c_str());
    return *this;
  }

  static constexpr IdType GetId(const char* src) {
    if (!src || !*src) return 0;
    return strlen(src) == 1 ? *src : (GetId(src + 1) << 8) + *src;
  }

  static const char* GetStr(IdType id) {
    static thread_local char str[5];
    auto i = 0u;
    for (i = 0u; i < 4 && id; ++i) {
      str[i] = id & 0xFF;
      id >>= 8;
    }
    str[i] = 0;
    return str;
  }
};

struct MarketData;
struct TradeTickHook {
  // OnTrade is not ensured to be called in the same thread of its algo
  virtual void OnTrade(DataSrc::IdType src, Security::IdType id,
                       const MarketData* md, time_t tm, double px,
                       double qty) noexcept = 0;
};

class Instrument;
class Indicator {
 public:
  typedef size_t IdType;
  virtual ~Indicator() {}
  virtual boost::python::object GetPyObject() const { return {}; }
  void AddListener(Instrument* inst) {
    Lock lock(m_);
    subs_.push_back(inst);
  }
  void Publish(IdType id);
  auto& m() { return m_; }

 protected:
  std::vector<Instrument*> subs_;
  mutable std::mutex m_;
  typedef std::lock_guard<std::mutex> Lock;
};

struct MarketData {
#ifdef BACKTEST
  typedef double Qty;
  typedef double Volume;
#else
  typedef int Qty;
  typedef int64_t Volume;
#endif
  time_t tm = 0;
  struct Trade {
    Qty qty = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double vwap = 0;
    Volume volume = 0;

    bool operator!=(const Trade& b) const {
      return volume != b.volume || close != b.close || high != b.high ||
             low != b.low;
    }

    void UpdatePx(double last_px) {
      if (!open) open = last_px;
      if (last_px > high) high = last_px;
      if (last_px < low || !low) low = last_px;
      close = last_px;
    }

    void UpdateVolume(Qty last_qty) {
      qty = last_qty;
      if (qty > 0) {
        vwap = (volume * vwap + close * qty) / (volume + qty);
        volume += qty;
      }
    }

    void Update(double px, Qty qty) {
      UpdatePx(px);
      UpdateVolume(qty);
    }
  };

  struct Quote {
    double ask_price = 0;
    double bid_price = 0;
    Qty ask_size = 0;
    Qty bid_size = 0;

    bool operator!=(const Quote& b) const {
      return ask_price != b.ask_price || ask_size != b.ask_size ||
             bid_price != b.bid_price || bid_size != b.bid_size;
    }

    bool operator==(const Quote& b) const { return !(*this != b); }
  };

  static inline const size_t kDepthSize = 5;
  typedef Quote Depth[kDepthSize];

  const Quote& quote() const { return depth[0]; }

  Trade trade;
  Depth depth;

  struct IndicatorManager {
    ~IndicatorManager() {
      for (auto& ind : inds) delete ind;
    }
    std::vector<Indicator*> inds;
    std::vector<TradeTickHook*> trade_tick_hooks;
  };

  template <typename T>
  void Set(T* value) {
    assert(T::kId < 16);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!mngr_) mngr_ = new IndicatorManager;
    if (mngr_->inds.size() <= T::kId) mngr_->inds.resize(T::kId + 1);
    mngr_->inds[T::kId] = value;
  }

  template <typename T = Indicator>
  const T* Get(Indicator::IdType id) const {
    if (!mngr_) return {};
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (id >= mngr_->inds.size()) return {};
    return dynamic_cast<T*>(mngr_->inds.at(id));
  }

  template <typename T>
  const T* Get() const {
    return Get<T>(T::kId);
  }

  void HookTradeTick(TradeTickHook* hook) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!mngr_) mngr_ = new IndicatorManager;
    mngr_->trade_tick_hooks.push_back(hook);
  }

  void UnhookTradeTick(TradeTickHook* hook) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!mngr_) return;
    mngr_->trade_tick_hooks.erase(
        std::remove(mngr_->trade_tick_hooks.begin(),
                    mngr_->trade_tick_hooks.end(), hook),
        mngr_->trade_tick_hooks.end());
  }

  void CheckTradeHook(DataSrc::IdType src, Security::IdType id) {
    if (!mngr_) return;
    if (mngr_->trade_tick_hooks.empty()) return;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    // to-do: make it async without using TaskPool
    for (auto& hook : mngr_->trade_tick_hooks) {
      hook->OnTrade(src, id, this, tm, trade.close, trade.qty);
    }
  }

#ifdef BACKTEST
  void Clear() {
    if (mngr_) {
      delete mngr_;
      mngr_ = nullptr;
    }
  }
#endif

 private:
  IndicatorManager* mngr_ = nullptr;
  static inline std::shared_mutex mutex_;
};

class MarketDataAdapter : public virtual NetworkAdapter {
 public:
  typedef tbb::concurrent_unordered_map<Security::IdType, MarketData>
      MarketDataMap;
  void Subscribe(const Security& sec) {
    tp_.AddTask([this, &sec]() {
      if (!subs_.insert(&sec).second) return;
      if (!connected()) return;
      SubscribeSync(sec);
    });
  }
  DataSrc::IdType src() const { return src_; }
  void Update(Security::IdType id, const MarketData::Quote& q,
              uint32_t level = 0, time_t tm = 0);
  void Update(Security::IdType id, double price, MarketData::Qty size,
              bool is_bid, uint32_t level = 0, time_t tm = 0);
  void Update(Security::IdType id, double last_price, MarketData::Qty last_qty,
              time_t tm = 0);
  void Update(Security::IdType id, double last_price, MarketData::Volume volume,
              double open, double high, double low, double vwap, time_t tm = 0);
  void UpdateMidAsLastPrice(Security::IdType id, time_t tm = 0);
  void UpdateAskPrice(Security::IdType id, double v, time_t tm = 0);
  void UpdateAskSize(Security::IdType id, double v, time_t tm = 0);
  void UpdateBidPrice(Security::IdType id, double v, time_t tm = 0);
  void UpdateBidSize(Security::IdType id, double v, time_t tm = 0);
  void UpdateLastPrice(Security::IdType id, double v, time_t tm = 0);
  void UpdateLastSize(Security::IdType id, double v, time_t tm = 0);

 protected:
  void ReSubscribeAll() {
    for (auto sec : subs_) SubscribeSync(*sec);
  }
  virtual void SubscribeSync(const Security& sec) noexcept = 0;

 protected:
  MarketDataMap* md_ = nullptr;
  std::atomic<int> request_counter_ = 0;
  tbb::concurrent_unordered_set<const opentrade::Security*> subs_;
  TaskPool tp_;

 private:
  DataSrc::IdType src_ = 0;
  friend class MarketDataManager;
};

class MarketDataManager : public AdapterManager<MarketDataAdapter, kMdPrefix>,
                          public Singleton<MarketDataManager> {
 public:
  MarketDataAdapter* Subscribe(const Security& sec, DataSrc::IdType src);
  void AddAdapter(MarketDataAdapter* adapter);
  const MarketData& Get(const Security& sec, DataSrc::IdType src = 0);
  // Lite version without subscription
  const MarketData& GetLite(Security::IdType id, DataSrc::IdType src = 0);
  MarketDataAdapter* GetDefault() const { return default_; }
  auto& srcs() const { return srcs_; }
  auto GetIndex(DataSrc::IdType src) {
    auto it = srcs_.find(src);
    if (it == srcs_.end()) return static_cast<decltype(it->second)>(-1);
    return it->second;
  }

 private:
  MarketDataAdapter* GetRoute(const Security& sec, DataSrc::IdType src);

 private:
  std::unordered_map<DataSrc::IdType, MarketDataAdapter::MarketDataMap>
      md_of_src_;
  MarketDataAdapter* default_;
  boost::unordered_map<std::pair<DataSrc::IdType, Exchange::IdType>,
                       std::vector<MarketDataAdapter*>>
      routes_;
  std::unordered_map<DataSrc::IdType, uint8_t> srcs_;
};

}  // namespace opentrade

#endif  // OPENTRADE_MARKET_DATA_H_
