#ifndef OPENTRADE_ALGO_H_
#define OPENTRADE_ALGO_H_

#include <tbb/atomic.h>
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "adapter.h"
#include "market_data.h"
#include "order.h"
#include "position.h"
#include "security.h"
#include "utility.h"

namespace opentrade {

class Instrument;

struct SecurityTuple {
  DataSrc src;
  const Security* sec = nullptr;
  const SubAccount* acc = nullptr;
  OrderSide side = kBuy;
  double qty = 0;
};

struct ParamDef {
  typedef std::variant<std::string, const char*, bool, int64_t, int32_t, double,
                       SecurityTuple>
      ValueScalar;
  typedef std::vector<ValueScalar> ValueVector;
  typedef std::variant<std::string, const char*, bool, int64_t, int32_t, double,
                       SecurityTuple, ValueVector>
      Value;
  std::string name;
  Value default_value;
  bool required = false;
  double min_value = 0;
  double max_value = 0;
  int precision = 0;
  bool editable = false;
};

typedef std::vector<ParamDef> ParamDefs;

class Algo : public Adapter {
 public:
  ~Algo();
  typedef uint32_t IdType;
  typedef std::unordered_map<std::string, ParamDef::Value> ParamMap;
  typedef std::shared_ptr<ParamMap> ParamMapPtr;
  void SetTimeout(std::function<void()> func, double seconds);
  void Async(std::function<void()> func) { SetTimeout(func, 0); }
  static bool Cancel(const Order& ord);

  virtual std::string OnStart(const ParamMap& params) noexcept { return {}; }
  virtual void OnModify(const ParamMap& params) noexcept {}
  virtual void OnStop() noexcept {}
  virtual void OnMarketTrade(const Instrument& inst, const MarketData& md,
                             const MarketData& md0) noexcept {}
  virtual void OnMarketQuote(const Instrument& inst, const MarketData& md,
                             const MarketData& md0) noexcept {}
  // for cross order, only kUnconfirmedNew and kFilled
  virtual void OnConfirmation(const Confirmation& cm) noexcept {}
  virtual const ParamDefs& GetParamDefs() noexcept {
    static const ParamDefs kEmptyParamDefs;
    return kEmptyParamDefs;
  }
  virtual void OnIndicator(Indicator::IdType id,
                           const Instrument& inst) noexcept {}

  virtual std::string Test() noexcept {
    assert(false);
    return {};
  }

  // initialize global variable here.
  // only called once when loading .so
  void Start() noexcept override {}

  bool is_active() const { return is_active_; }
  IdType id() const { return id_; }
  const std::string& token() const { return token_; }
  const User& user() const { return *user_; }
  void set_user(const User* user) { user_ = user; }

 protected:
  Instrument* Subscribe(const Security& sec, DataSrc src = {},
                        bool listen = true, Instrument* parent = nullptr);
  void Stop();
  Order* Place(const Contract& contract, Instrument* inst);
  void Cross(double qty, double price, OrderSide side, const SubAccount* acc,
             Instrument* inst);

 private:
  const User* user_ = nullptr;
  bool is_active_ = true;
  IdType id_ = 0;
  std::string token_;
  std::unordered_set<Instrument*> instruments_;
  friend class AlgoManager;
  friend class Backtest;
};

class Instrument {
 public:
  typedef std::unordered_set<Order*> Orders;
  Instrument(Algo* algo, const Security& sec, DataSrc src)
      : algo_(algo), sec_(sec), src_(src) {}
  Algo& algo() { return *algo_; }
  const Algo& algo() const { return *algo_; }
  const Instrument* parent() const { return parent_; }
  auto src_idx() const { return src_idx_; }
  const Security& sec() const { return sec_; }
  DataSrc src() const { return src_; }
  const MarketData& md() const { return *md_; }
  const Orders& active_orders() const { return active_orders_; }
  double bought_qty() const { return bought_qty_; }
  double sold_qty() const { return sold_qty_; }
  double outstanding_buy_qty() const { return outstanding_buy_qty_; }
  double outstanding_sell_qty() const { return outstanding_sell_qty_; }
  double net_qty() const { return Round6(bought_qty_ - sold_qty_); }
  double net_cx_qty() const { return Round6(bought_cx_qty_ - sold_cx_qty_); }
  double total_qty() const { return Round6(bought_qty_ + sold_qty_); }
  double total_cx_qty() const { return Round6(bought_cx_qty_ + sold_cx_qty_); }
  double net_outstanding_qty() const {
    return Round6(outstanding_buy_qty_ - outstanding_sell_qty_);
  }
  double total_outstanding_qty() const {
    return Round6(outstanding_buy_qty_ + outstanding_sell_qty_);
  }
  double total_exposure() const {
    return Round6(total_qty() - total_cx_qty() + total_outstanding_qty());
  }
  size_t id() const { return id_; }

  void Cancel() {
    for (auto ord : active_orders_) algo_->Cancel(*ord);
  }

  void UnListen() { listen_ = false; }
  bool listen() const { return listen_; }
  void HookTradeTick(TradeTickHook* hook) {
    const_cast<MarketData*>(md_)->HookTradeTick(hook);
  }
  void UnhookTradeTick(TradeTickHook* hook) {
    const_cast<MarketData*>(md_)->UnhookTradeTick(hook);
  }
  void Subscribe(Indicator::IdType id, bool listen = false);
  void SubscribeByName(const std::string& name, bool listen = false);
  template <typename T>
  const T* Get() const {
    return md_->Get<T>();
  }
  template <typename T = Indicator>
  const T* Get(Indicator::IdType id) const {
    return md_->Get<T>(id);
  }

 private:
  Algo* algo_ = nullptr;
  const Security& sec_;
  const MarketData* md_ = nullptr;
  const DataSrc src_;
  Orders active_orders_;  // cross order not inserted here
  double bought_qty_ = 0;
  double sold_qty_ = 0;
  double bought_cx_qty_ = 0;
  double sold_cx_qty_ = 0;
  double outstanding_buy_qty_ = 0;  // cross order not impact this
  double outstanding_sell_qty_ = 0;
  size_t id_ = 0;
  bool listen_ = true;
  uint8_t src_idx_ = -1;  // for fast looking up in price consolidation
  Instrument* parent_ = nullptr;
  friend class AlgoManager;
  friend class Algo;
  static inline std::atomic<size_t> id_counter_ = 0;
};

class AlgoRunner {
 public:
  AlgoRunner() {}
#ifdef UNIT_TEST
  explicit AlgoRunner(std::thread::id tid) : tid_(tid) {}
#endif
  void operator()();

 private:
  boost::unordered_map<std::pair<DataSrc::IdType, Security::IdType>,
                       std::pair<MarketData, std::list<Instrument*>>>
      instruments_;
  tbb::concurrent_unordered_map<std::pair<DataSrc::IdType, Security::IdType>,
                                tbb::atomic<uint32_t>>
      md_refs_;
  std::thread::id tid_;
  boost::unordered_set<std::pair<DataSrc::IdType, Security::IdType>> dirties_;
  std::mutex mutex_;
  typedef std::lock_guard<std::mutex> LockGuard;
  friend class AlgoManager;
  friend class Backtest;
};

class Connection;

class AlgoManager : public AdapterManager<Algo>, public Singleton<AlgoManager> {
 public:
  static void Initialize();
  Algo* Spawn(Algo::ParamMapPtr params, const std::string& name,
              const User& user, const std::string& params_raw,
              const std::string& token);
  template <typename T>
  void Modify(const T& id, Algo::ParamMapPtr params) {
    Modify(Get(id), params);
  }
  void Modify(Algo* algo, Algo::ParamMapPtr params);
  void Run(int nthreads);
  void StartPermanents();
  void Update(DataSrc::IdType src, Security::IdType id);
  void Stop();
  void Stop(Algo::IdType id);
  void Stop(const std::string& token);
  void Stop(Security::IdType sec, SubAccount::IdType acc);
  void Handle(Confirmation::Ptr cm);
  void SetTimeout(const Algo& algo, std::function<void()> func, double seconds);
  bool IsSubscribed(DataSrc::IdType src, Security::IdType id) {
    return md_refs_[std::make_pair(src, id)] > 0;
  }
  void Register(Instrument* inst);
  void Persist(const Algo& algo, const std::string& status,
               const std::string& body);
  void LoadStore(uint32_t seq0 = 0, Connection* conn = nullptr);
  Algo* Get(const Algo::IdType& id) { return FindInMap(algos_, id); }
  Algo* Get(const std::string& token) {
    return FindInMap(algo_of_token_, token);
  }
  void Cancel(Instrument* inst);
  auto tid(const Algo& algo) const {
    return runners_[algo.id() % threads_.size()].tid_;
  }

 protected:
  std::atomic<Algo::IdType> algo_id_counter_ = 0;
  tbb::concurrent_unordered_map<Algo::IdType, Algo*> algos_;
  tbb::concurrent_unordered_map<std::string, Algo*> algo_of_token_;
  tbb::concurrent_unordered_multimap<
      std::pair<Security::IdType, SubAccount::IdType>, Algo*>
      algos_of_sec_acc_;
  tbb::concurrent_unordered_map<std::pair<DataSrc::IdType, Security::IdType>,
                                tbb::atomic<uint32_t>>
      md_refs_;
  AlgoRunner* runners_ = nullptr;
  std::vector<std::thread> threads_;
#ifdef BACKTEST
  struct Strand {
    void post(std::function<void()> func) { kTimers.emplace(0, func); }
  };
#else
  struct Strand {
    // clang-format off
#ifdef UNIT_TEST
    virtual
#endif
    void post(std::function<void()> func) {
      io->post(func);
    }
    // clang-format on
    boost::asio::io_service* io;
  };
  std::vector<std::unique_ptr<boost::asio::io_service::work>> works_;
#endif
  Strand* strands_ = nullptr;
  std::ofstream of_;
  uint32_t seq_counter_ = 0;
  friend class AlgoRunner;
  friend class Backtest;
};

}  // namespace opentrade

#endif  // OPENTRADE_ALGO_H_
