#include "algo.h"

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <mutex>
#include <sstream>

#include "connection.h"
#include "cross_engine.h"
#include "exchange_connectivity.h"
#include "indicator_handler.h"
#include "logger.h"
#include "python.h"
#include "server.h"
#include "stop_book.h"

namespace fs = boost::filesystem;

namespace opentrade {

static auto kPath = kStorePath / "algos";
static thread_local std::string kError;

inline void AlgoRunner::operator()() {
  assert(std::this_thread::get_id() == tid_);

  for (;;) {
    decltype(dirties_)::value_type key;
    {
      LockGuard lock(mutex_);
      if (dirties_.empty()) return;
      auto it = dirties_.begin();
      static thread_local uint32_t kSeed = time(NULL);
      if (dirties_.size() > 1) {
        auto n = rand_r(&kSeed) % std::min(3lu, dirties_.size());
        if (n > 0) std::advance(it, n);
      }
      key = *it;
      dirties_.erase(it);
    }
    auto& pair = instruments_[key];
    auto& insts = pair.second;
    auto it = insts.begin();
    if (it == insts.end()) continue;
    auto md = (*it)->md();
    auto& md0 = pair.first;
    bool trade_update = md0.trade != md.trade;
    bool quote_update = md0.quote() != md.quote();
    while (it != insts.end()) {
      auto& algo = (*it)->algo();
      if (!algo.is_active() || !(*it)->listen()) {
        it = insts.erase(it);
        md_refs_[key]--;
        assert(md_refs_[key] >= 0);
        assert(md_refs_[key] == insts.size());
        assert(AlgoManager::Instance().md_refs_[key] > 0);
        AlgoManager::Instance().md_refs_[key]--;
        assert(AlgoManager::Instance().md_refs_[key] >= 0);
        continue;
      }
      if (trade_update) algo.OnMarketTrade(**it, md, md0);
      if (quote_update) algo.OnMarketQuote(**it, md, md0);
      it++;
    }
    md0 = md;
  }
}

inline void AlgoManager::Register(Instrument* inst) {
  auto& runner = runners_[inst->algo().id() % threads_.size()];
  auto key = std::make_pair(inst->src(), inst->sec().id);
  auto& pair = runner.instruments_[key];
  if (pair.second.empty()) {
    pair.first = inst->md();
  }
  assert(std::find(pair.second.begin(), pair.second.end(), inst) ==
         pair.second.end());
  runner.md_refs_[key]++;
  md_refs_[key]++;
  pair.second.push_back(inst);
  assert(std::this_thread::get_id() == runner.tid_);
}

void AlgoManager::Modify(Algo* algo, Algo::ParamMapPtr params) {
  if (!algo || !params) return;
  algo->Async([params, algo]() { algo->OnModify(*params.get()); });
}

Algo* AlgoManager::Spawn(Algo::ParamMapPtr params, const std::string& name,
                         const User& user, const std::string& params_raw,
                         const std::string& token) {
  Algo* algo = nullptr;
  if (params) {
    auto adapter = GetAdapter(name);
    if (!adapter) return nullptr;
    algo = static_cast<Algo*>(adapter->Clone());
  } else {
    algo = Python::LoadTest(name, token);
  }
  if (!algo) return nullptr;
  for (;;) {
    algo->id_ = ++algo_id_counter_;
    // assign python algo on 0th thread, the others shares the other threads
    if (threads_.size() > 1) {
      if (dynamic_cast<Python*>(algo)) {
        if (algo->id_ % threads_.size() == 0) break;
      } else {
        if (algo->id_ % threads_.size() != 0) break;
      }
    } else {
      break;
    }
  }
  algo->user_ = &user;
  algo->token_ = token;
  algo->is_active_ = true;  // for permanent in backtest
  algos_.emplace(algo->id_, algo);
  if (!token.empty()) algo_of_token_.emplace(token, algo);
  std::string disabled;
  user.CheckDisabled("user", &disabled);
  if (params) {
    for (auto& pair : *params) {
      if (auto pval = std::get_if<SecurityTuple>(&pair.second)) {
        if (pval->acc) {
          if (disabled.empty())
            pval->acc->CheckDisabled("sub_account", &disabled);
          if (pval->sec) {
            if (disabled.empty()) {
              auto broker =
                  pval->acc->GetBrokerAccount(pval->sec->exchange->id);
              if (broker) broker->CheckDisabled("broker_account", &disabled);
            }
            if (disabled.empty()) {
              StopBookManager::Instance().CheckStop(*pval->sec, pval->acc,
                                                    &disabled);
            }
            algos_of_sec_acc_.insert(std::make_pair(
                std::make_pair(pval->sec->id, pval->acc->id), algo));
          }
        }
      }
    }
  }
  if (dynamic_cast<IndicatorHandler*>(algo)) return algo;
  Persist(*algo, "new", params ? params_raw : "{\"test\":true}");
  algo->Async([params, algo, disabled]() {
    if (!disabled.empty()) {
      kError = disabled;
    } else {
      kError = params ? algo->OnStart(*params.get()) : algo->Test();
    }
    if (!kError.empty()) {
      algo->Stop();
#ifdef BACKTEST
      LOG_ERROR(kError);
#endif
    }
    kError.clear();
  });
  return algo;
}

void AlgoManager::Initialize() {
  auto& self = Instance();
  self.of_.open(kPath.c_str(), std::ofstream::app);
  if (!self.of_.good()) {
    LOG_FATAL("Failed to write file: " << kPath.c_str() << ": "
                                       << strerror(errno));
  }
  self.LoadStore();
  self.algo_id_counter_ += 100;
  LOG_INFO("Algo id starts from " << self.algo_id_counter_);
  self.seq_counter_ += 100;
}

void AlgoManager::Update(DataSrc::IdType src, Security::IdType id) {
  auto key = std::make_pair(src, id);
  for (auto i = 0u; i < threads_.size(); ++i) {
    auto& runner = runners_[i];
    if (runner.md_refs_[key] > 0) {
      auto should_run = false;
      {
        AlgoRunner::LockGuard lock(runner.mutex_);
        should_run = runner.dirties_.empty();
        runner.dirties_.insert(key);
      }
      if (should_run) strands_[i].post([&runner]() { runner(); });
    }
  }
}

void AlgoManager::Run(int nthreads) {
#ifdef BACKTEST
  threads_.resize(1);
  strands_ = new Strand[1]{};
  runners_ = new AlgoRunner[1]{};
  runners_[0].tid_ = std::this_thread::get_id();
#else
  nthreads = std::max(1, nthreads);
  runners_ = new AlgoRunner[nthreads]{};
  LOG_INFO("algo_threads=" << nthreads);
  threads_.reserve(nthreads);
  strands_ = new Strand[nthreads]{};
  works_.resize(nthreads);
  for (auto i = 0; i < nthreads; ++i) {
    strands_[i].io = new boost::asio::io_service;
    works_[i].reset(new boost::asio::io_service::work(*strands_[i].io));
    threads_.emplace_back([this, i]() { strands_[i].io->run(); });
    runners_[i].tid_ = threads_[i].get_id();
  }
  StartPermanents();
#endif
}

void AlgoManager::StartPermanents() {
  for (auto& pair : adapters()) {
    auto ih = dynamic_cast<IndicatorHandler*>(pair.second);
    if (pair.first.at(0) != '_' && !ih) continue;
#ifdef BACKTEST
    if (ih) {
      assert(ih->create_func());
    }
#endif
    auto user_name = pair.second->config("user");
    auto user = AccountManager::Instance().GetUser(user_name);
    auto algo = Spawn(std::make_shared<Algo::ParamMap>(), pair.first,
                      user ? *user : kEmptyUser, "{}", "");
    if (algo) {
      LOG_INFO("Started " << pair.first << ", algo id=" << algo->id());
    } else {
      LOG_ERROR("Failed to start" << pair.first);
    }
  }

  for (auto& pair : algos_) {
    auto ih = dynamic_cast<IndicatorHandler*>(pair.second);
    if (!ih) continue;
    IndicatorHandlerManager::Instance().Register(ih);
  }

  for (auto& pair : algos_) {
    auto ih = dynamic_cast<IndicatorHandler*>(pair.second);
    if (!ih) continue;
    ih->Async([ih]() { ih->OnStart(); });
  }
}

void AlgoManager::Handle(Confirmation::Ptr cm) {
  assert(cm->order->inst);
  auto inst = const_cast<Instrument*>(cm->order->inst);
  static std::mutex kMutex;
  {
    std::lock_guard<std::mutex> lock(kMutex);
    switch (cm->exec_type) {
      case kPartiallyFilled:
      case kFilled:
        if (cm->exec_trans_type == kTransNew) {
          if (cm->order->IsBuy()) {
            if (cm->order->type != kCX)
              inst->outstanding_buy_qty_ -= cm->last_shares;
            else
              inst->bought_cx_qty_ += cm->last_shares;
            inst->bought_qty_ += cm->last_shares;
          } else {
            if (cm->order->type != kCX)
              inst->outstanding_sell_qty_ -= cm->last_shares;
            else
              inst->sold_cx_qty_ += cm->last_shares;
            inst->sold_qty_ += cm->last_shares;
          }
          if (cm->order->type != kCX) CrossEngine::Instance().UpdateTrade(cm);
        } else if (cm->exec_trans_type == kTransCancel) {
          if (cm->order->IsBuy())
            inst->bought_qty_ -= cm->last_shares;
          else
            inst->sold_qty_ -= cm->last_shares;
        }
        break;
      case kCanceled:
      case kRejected:
      case kExpired:
      case kCalculated:
      case kDoneForDay:
        if (cm->order->IsBuy())
          inst->outstanding_buy_qty_ -= cm->leaves_qty;
        else
          inst->outstanding_sell_qty_ -= cm->leaves_qty;
        break;
      case kUnconfirmedNew:
      case kUnconfirmedCancel:
      case kPendingCancel:
      case kCancelRejected:
      case kPendingNew:
      case kNew:
      case kSuspended:
      case kRiskRejected:
        break;
      default:
        return;
    }
  }
  inst->algo().Async([cm, inst]() {
    switch (cm->exec_type) {
      case kPartiallyFilled:
      case kFilled:
        if (!cm->order->IsLive()) inst->active_orders_.erase(cm->order);
        inst->algo().OnConfirmation(*cm.get());
        break;
      case kCanceled:
      case kRejected:
      case kExpired:
      case kCalculated:
      case kDoneForDay:
        inst->active_orders_.erase(cm->order);
        inst->algo().OnConfirmation(*cm.get());
        break;
      case kUnconfirmedNew:
      case kUnconfirmedCancel:
      case kPendingCancel:
      case kCancelRejected:
      case kPendingNew:
      case kNew:
      case kSuspended:
      case kRiskRejected:
        inst->algo().OnConfirmation(*cm.get());
        break;
      default:
        break;
    }
  });
}

void AlgoManager::Stop() {
  for (auto& pair : algos_) {
    auto algo = pair.second;
    algo->Async([algo]() { algo->Stop(); });
  }
}

void AlgoManager::Stop(Algo::IdType id) {
  auto algo = FindInMap(algos_, id);
  if (algo) algo->Async([algo]() { algo->Stop(); });
}

void AlgoManager::Stop(const std::string& token) {
  auto algo = FindInMap(algo_of_token_, token);
  if (algo) algo->Async([algo]() { algo->Stop(); });
}

void AlgoManager::Stop(Security::IdType sec, SubAccount::IdType acc) {
  if (sec > 0) {
    auto range = algos_of_sec_acc_.equal_range(std::make_pair(sec, acc));
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second->is_active()) Stop(it->second->id());
    }
  } else {
    for (auto& pair : algos_of_sec_acc_) {
      if (pair.first.second == acc && pair.second->is_active())
        Stop(pair.second->id());
    }
  }
}

void AlgoManager::Persist(const Algo& algo, const std::string& status,
                          const std::string& body) {
#ifdef BACKTEST
  return;
#endif
  kWriteTaskPool.AddTask([this, &algo, status, body]() {
    std::stringstream ss;
    ss << GetTime() << ' ' << algo.name() << ' ' << status << ' ' << body;
    auto str = ss.str();
    auto seq = ++seq_counter_;
    Server::Publish(algo, status, body, seq);
    of_.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
    uint32_t n = str.size();
    of_.write(reinterpret_cast<const char*>(&n), sizeof(n));
    auto uid = algo.user().id;
    of_.write(reinterpret_cast<const char*>(&uid), sizeof(uid));
    auto aid = algo.id();
    of_.write(reinterpret_cast<const char*>(&aid), sizeof(aid));
    of_ << ss.str() << '\0' << std::endl;
  });
}

void AlgoManager::LoadStore(uint32_t seq0, Connection* conn) {
  if (!fs::file_size(kPath)) return;
  boost::iostreams::mapped_file_source m(kPath.string());
  auto p = m.data();
  auto p_end = p + m.size();
  auto ln = 0;
  while (p + 8 < p_end) {
    ln++;
    auto seq = *reinterpret_cast<const uint32_t*>(p);
    if (!conn) seq_counter_ = seq;
    p += 4;
    auto n = *reinterpret_cast<const uint32_t*>(p);
    if (p + n + 10 + sizeof(User::IdType) > p_end) break;
    p += 4;
    auto user_id = *reinterpret_cast<const User::IdType*>(p);
    p += sizeof(User::IdType);
    auto id = *reinterpret_cast<const uint32_t*>(p);
    if (!conn && id > algo_id_counter_) algo_id_counter_ = id;
    p += 4;
    auto payload = p;
    p += n + 2;  // body + '\0' + '\n'
    if (!conn || seq <= seq0) continue;
    if (!conn->user_->is_admin && conn->user_->id != user_id) continue;
    int32_t tm;
    char name[n];
    char status[n];
    char body[n];
    *body = 0;
    if (sscanf(payload, "%d %s %s %[^\1]", &tm, name, status, body) < 3) {
      LOG_ERROR("Failed to parse algo line #" << ln);
      continue;
    }
    conn->Send(id, tm, "", name, status, body, seq, true);
  }
  if (!conn && p != p_end) {
    LOG_FATAL("Corrupted algo file: " << kPath.c_str()
                                      << ", please fix it first");
  }
}

Algo::~Algo() {
  for (auto& inst : instruments_) delete inst;
}

Instrument* Algo::Subscribe(const Security& sec, DataSrc src, bool listen,
                            Instrument* parent) {
  assert(std::this_thread::get_id() == AlgoManager::Instance().tid(*this));
  auto adapter = MarketDataManager::Instance().Subscribe(sec, src);
  assert(adapter);
  auto inst = new Instrument(this, sec, DataSrc(adapter->src()));
  inst->parent_ = parent;
  if (parent)
    inst->src_idx_ = MarketDataManager::Instance().GetIndex(adapter->src());
  inst->md_ = &MarketDataManager::Instance().Get(sec, adapter->src());
  inst->id_ = ++Instrument::id_counter_;
  inst->listen_ = listen;
  std::atomic_thread_fence(std::memory_order_release);
  instruments_.insert(inst);
  if (listen) AlgoManager::Instance().Register(inst);
  return inst;
}

void Algo::Stop() {
  assert(std::this_thread::get_id() == AlgoManager::Instance().tid(*this));
  if (is_active_) {
    is_active_ = false;
    for (auto inst : instruments_) inst->Cancel();
    AlgoManager::Instance().Persist(
        *this, kError.empty() ? "terminated" : "failed", kError);
    OnStop();
  }
}

inline void AlgoManager::SetTimeout(const Algo& algo,
                                    std::function<void()> func,
                                    double seconds) {
  if (seconds < 0) seconds = 0;
#ifdef BACKTEST
  kTimers.emplace(kTime + seconds * kMicroInSec, [&algo, func]() {
    if (algo.is_active()) func();
  });
#else
  if (seconds <= 0) {
    strands_[algo.id() % threads_.size()].post(func);
    return;
  }
  auto t = new boost::asio::deadline_timer(
      *strands_[algo.id() % threads_.size()].io,
      boost::posix_time::microseconds((int64_t)(seconds * kMicroInSec)));
  t->async_wait([&algo, func, t](auto) {
    if (algo.is_active()) func();
    delete t;
  });
#endif
}

void Algo::SetTimeout(std::function<void()> func, double seconds) {
  AlgoManager::Instance().SetTimeout(*this, func, seconds);
}

void AlgoManager::Cancel(Instrument* inst) {
  inst->algo().Async([=]() { inst->Cancel(); });
}

Order* Algo::Place(const Contract& contract, Instrument* inst) {
  assert(inst);
  if (!is_active_ || !inst) return nullptr;
  auto ord = contract.type == kCX ? new CrossOrder{} : new Order{};
  (Contract&)* ord = contract;
  ord->algo_id = id_;
  ord->user = user_;
  ord->inst = inst;
  ord->sec = &inst->sec();
  auto ok = ExchangeConnectivityManager::Instance().Place(ord);
  if (!ok) return nullptr;
  if (contract.type == kCX) return ord;
  inst->active_orders_.insert(ord);
  if (ord->IsBuy())
    inst->outstanding_buy_qty_ += ord->qty;
  else
    inst->outstanding_sell_qty_ += ord->qty;
  return ord;
}

void Algo::Cross(double qty, double price, OrderSide side,
                 const SubAccount* acc, Instrument* inst) {
  Contract c;
  c.side = side;
  c.qty = qty;
  c.price = price;
  c.sub_account = acc;
  c.type = kCX;
  Place(c, inst);
}

bool Algo::Cancel(const Order& ord) {
  return ExchangeConnectivityManager::Instance().Cancel(ord);
}

void Instrument::Subscribe(Indicator::IdType id, bool listen) {
  auto ih = IndicatorHandlerManager::Instance().Get(id);
  if (ih) ih->Subscribe(this, listen);
}

void Instrument::SubscribeByName(const std::string& name, bool listen) {
  auto& m = IndicatorHandlerManager::Instance().name2id();
  auto it = m.find(name);
  if (it != m.end()) Subscribe(it->second, listen);
}

}  // namespace opentrade
