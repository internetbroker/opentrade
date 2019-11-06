#ifdef BACKTEST

#include "backtest.h"

#include <boost/iostreams/device/mapped_file.hpp>

#include "cross_engine.h"
#include "indicator_handler.h"
#include "logger.h"
#include "simulator.h"

namespace fs = boost::filesystem;

namespace opentrade {

decltype(auto) GetSecurities(std::ifstream& ifs, const char* fn, bool* binary,
                             const std::set<std::string>& used_symbols) {
  std::string line;
  if (!std::getline(ifs, line)) {
    LOG_FATAL("Invalid file: " << fn);
  }
  char a[256];
  *a = 0;
  char b[256];
  *b = 0;
  char c[256];
  *c = 0;
  std::vector<const Security*> out;
  if (sscanf(line.c_str(), "%s %s %s", a, b, c) < 2 ||
      strcasecmp(a, "@begin")) {
    LOG_FATAL("Invalid file: " << fn);
  }
  *binary = !strncasecmp(c, "bin", 3);
  std::unordered_map<std::string, const Security*> sec_map;
  if (!strcasecmp(b, "bbgid")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      if (*pair.second->bbgid) sec_map[pair.second->bbgid] = pair.second;
    }
  } else if (!strcasecmp(b, "isin")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      if (*pair.second->isin) sec_map[pair.second->isin] = pair.second;
    }
  } else if (!strcasecmp(b, "cusip")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      if (*pair.second->cusip) sec_map[pair.second->cusip] = pair.second;
    }
  } else if (!strcasecmp(b, "sedol")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      if (*pair.second->sedol) sec_map[pair.second->sedol] = pair.second;
    }
  } else if (!strcasecmp(b, "id")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      sec_map[std::to_string(pair.second->id)] = pair.second;
    }
  } else if (!strcasecmp(b, "symbol")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      sec_map[std::string(pair.second->exchange->name) + " " +
              pair.second->symbol] = pair.second;
    }
  } else if (!strcasecmp(b, "local_symbol")) {
    for (auto& pair : SecurityManager::Instance().securities()) {
      if (*pair.second->local_symbol)
        sec_map[std::string(pair.second->exchange->name) + " " +
                pair.second->local_symbol] = pair.second;
    }
  } else {
    LOG_FATAL("Invalid file: " << fn);
  }

  while (std::getline(ifs, line)) {
    if (!strcasecmp(line.c_str(), "@end")) break;
    auto sec = sec_map[line];
    if (!sec) {
      LOG_ERROR("Unknown security on line " << line << " of " << fn);
      continue;
    }
    if (used_symbols.size() && used_symbols.find(line) == used_symbols.end())
      sec = nullptr;
    out.push_back(sec);
  }
  LOG_INFO(out.size() << " securities in " << fn);

  return out;
}

struct SecTuple {
  const Security* sec;
  Simulator* sim;
  Simulator::Orders* actives;
  double adj_px;
  double adj_vol;
};
typedef std::vector<SecTuple> SecTuples;

struct Tick {
  SecTuple* st;
  uint32_t ms;
  char type;
  double px;
  double qty;
  bool operator<(const Tick& b) const { return ms < b.ms; }
};
typedef std::vector<Tick> Ticks;

bool LoadTickFile(const char* fn, Simulator* sim,
                  const boost::gregorian::date& date, SecTuples* sts,
                  std::ifstream& ifs, bool* binary,
                  const std::set<std::string>& used_symbols) {
  *binary = true;
  ifs.open(fn);
  if (!ifs.good()) return false;

  LOG_INFO("Loading " << fn);
  auto secs0 = GetSecurities(ifs, fn, binary, used_symbols);
  sts->clear();
  sts->resize(secs0.size());
  auto date_num = date.year() * 10000 + date.month() * 100 + date.day();
  for (auto i = 0u; i < secs0.size(); ++i) {
    auto sec = secs0[i];
    if (!sec) continue;
    auto& adjs = sec->adjs;
    auto it = std::upper_bound(sec->adjs.begin(), sec->adjs.end(),
                               Security::Adj(date_num));
    if (it == adjs.end())
      (*sts)[i] = SecTuple{sec, sim, &sim->active_orders()[sec->id], 1., 1.};
    else
      (*sts)[i] =
          SecTuple{sec, sim, &sim->active_orders()[sec->id], it->px, it->vol};
  }
  return true;
}

inline Tick ReadTextTickFile(std::ifstream& ifs, uint32_t to_tm, SecTuples* sts,
                             Ticks* ticks) {
  static const int kLineLength = 128;
  char line[kLineLength];
  while (ifs.getline(line, sizeof(line))) {
    Tick t;
    uint32_t i;
    uint32_t hmsm;
    if (sscanf(line, "%u %u %c %lf %lf", &hmsm, &i, &t.type, &t.px, &t.qty) !=
        5)
      continue;
    if (i >= sts->size()) continue;
    auto& st = (*sts)[i];
    if (!st.sec) continue;
    t.st = &st;
    t.px *= st.adj_px;
    if (!t.px) continue;
    t.qty *= st.adj_vol;
    auto hms = hmsm / 1000;
    t.ms = (hms / 10000 * 3600 + hms % 10000 / 100 * 60 + hms % 100) * 1000 +
           hmsm % 1000;
    if (t.ms > to_tm) return t;
    ticks->push_back(t);
  }
  return {};
}

inline Tick ReadBinaryTickFile(const char** pp, const char* p_end,
                               uint32_t to_tm, SecTuples* sts, Ticks* ticks) {
  auto& p = *pp;
  while (p < p_end) {
    Tick t;
    t.ms = *reinterpret_cast<const uint32_t*>(p);
    p += 4;
    auto i = *reinterpret_cast<const uint16_t*>(p);
    p += 2;
    t.type = *p;
    p += 1;
    t.px = *reinterpret_cast<const double*>(p);
    p += 8;
    t.qty = *reinterpret_cast<const uint32_t*>(p);
    p += 4;
    if (i >= sts->size()) continue;
    auto& st = (*sts)[i];
    if (!st.sec) continue;
    t.st = &st;
    t.px *= st.adj_px;
    if (!t.px) continue;
    t.qty *= st.adj_vol;
    if (t.ms > to_tm) return t;
    ticks->push_back(t);
  }
  return {};
}

void Backtest::Play(const boost::gregorian::date& date) {
  skip_ = false;
  boost::posix_time::ptime pt(date);
  auto tm = boost::posix_time::to_tm(pt);
  auto tm0 = mktime(&tm);
  auto tm0_us = tm0 * kMicroInSec;
  kTime = tm0_us;
  localtime_r(&tm0, &tm);
  for (auto& pair : SecurityManager::Instance().exchanges()) {
    pair.second->utc_time_offset = tm.tm_gmtoff;
  }

  char fn[256];
  SecTuples sts[simulators_.size()];
  std::ifstream ifs[simulators_.size()];
  bool binaries[simulators_.size()];
  std::vector<std::pair<const char*, const char*>> fpos(simulators_.size());
  boost::iostreams::mapped_file_source mmfiles[simulators_.size()];
  auto n = 0;
  for (auto i = 0u; i < simulators_.size(); ++i) {
    strftime(fn, sizeof(fn), simulators_[i].first.c_str(), &tm);
    if (LoadTickFile(fn, simulators_[i].second, date, &sts[i], ifs[i],
                     &binaries[i], used_symbols_)) {
      LOG_DEBUG("Start to play back " << fn);
      if (binaries[i]) {
        mmfiles[i].open(fn);
        auto p = mmfiles[i].data();
        auto p_end = p + mmfiles[i].size();
        p += ifs[i].tellg();
        ifs[i].close();
        if ((p_end - p) % 19) {
          LOG_FATAL("Invalid binary file: " << fn);
        }
        fpos[i] = std::make_pair(p, p_end);
      }
      ++n;
    }
  }
  if (!n) return;

  AlgoManager::Instance().StartPermanents();
  if (on_start_of_day_) {
    try {
      on_start_of_day_(obj_, kOpentrade.attr("get_datetime")().attr("date")());
    } catch (const bp::error_already_set& err) {
      PrintPyError("on_start_of_day", true);
      return;
    }
  }

  std::vector<Tick> last_ticks(simulators_.size());
  static const uint32_t kNSteps = 240;
  static const uint32_t kStep = (kSecondsOneDay * 1000) / kNSteps;
  Ticks ticks;
  for (auto to_tm = kStep; to_tm <= kSecondsOneDay * 1000 && !skip_;
       to_tm += kStep) {
    ticks.clear();
    for (auto i = 0u; i < simulators_.size(); ++i) {
      auto& t = last_ticks[i];
      if (t.st) {
        if (t.ms > to_tm) continue;
        ticks.push_back(t);
      }
      t = binaries[i] ? ReadBinaryTickFile(&fpos[i].first, fpos[i].second,
                                           to_tm, &sts[i], &ticks)
                      : ReadTextTickFile(ifs[i], to_tm, &sts[i], &ticks);
    }
    if (simulators_.size() > 1) std::sort(ticks.begin(), ticks.end());
    for (auto& t : ticks) {
      if (skip_) break;
      auto tm = tm0_us + t.ms * 1000lu;
      if (tm < kTime) tm = kTime;
      auto it = kTimers.begin();
      while (it != kTimers.end() && it->first <= tm) {
        if (it->first > kTime) kTime = it->first;
        it->second();
        kTimers.erase(it);
        // do not use it = kTimers.erase(it) in case smaller timer inserted
        it = kTimers.begin();
      }
      if (tm > kTime) kTime = tm;

      t.st->sim->HandleTick(*t.st->sec, t.type, t.px, t.qty, trade_hit_ratio_,
                            t.st->actives);
    }
  }

  PositionManager::Instance().UpdatePnl();

  if (on_end_of_day_) {
    try {
      on_end_of_day_(obj_, kOpentrade.attr("get_datetime")().attr("date")());
    } catch (const bp::error_already_set& err) {
      PrintPyError("on_end_of_day", true);
    }
  }

  Clear();
}

void Backtest::Clear() {
  auto& algo_mngr = AlgoManager::Instance();
  for (auto& pair : algo_mngr.algos_) {
    pair.second->Stop();
    if (pair.second->create_func()) delete pair.second;
  }
  algo_mngr.runners_[0].dirties_.clear();
  algo_mngr.runners_[0].instruments_.clear();
  algo_mngr.runners_[0].md_refs_.clear();
  algo_mngr.md_refs_.clear();
  algo_mngr.algos_.clear();
  algo_mngr.algo_of_token_.clear();
  algo_mngr.algos_of_sec_acc_.clear();
  auto& gb = GlobalOrderBook::Instance();
  for (auto& pair : gb.orders_) delete pair.second;
  gb.orders_.clear();
  gb.exec_ids_.clear();
  for (auto& pair : simulators_) pair.second->active_orders().clear();
  kTimers.clear();
  IndicatorHandlerManager::Instance().ihs_.clear();
  IndicatorHandlerManager::Instance().name2id_.clear();
  for (auto& pair : simulators_) {
    pair.second->ResetData();
  }
  auto& secs = CrossEngine::Instance().securities_;
  for (auto& pair : secs) delete pair.second;
  secs.clear();
}

void Backtest::AddSimulator(const std::string& fn_tmpl,
                            const std::string& name) {
  auto sim = new Simulator(of_);
  sim->set_name(name);
  Adapter::StrMap params;
  std::stringstream tmp;
  for (auto& pair : SecurityManager::Instance().exchanges()) {
    if (!strcmp(pair.second->name, "default")) continue;
    if (!tmp.str().empty()) tmp << ',';
    tmp << pair.second->name;
  }
  params["src"] = name;
  params["markets"] = tmp.str();
  sim->set_config(params);
  simulators_.emplace_back(fn_tmpl, sim);
  auto& acc_mngr = AccountManager::Instance();
  auto b = new BrokerAccount();
  b->name = name.empty() ? "backtest" : name.c_str();
  b->adapter = sim;
  b->id = acc_mngr.broker_accounts_.size();
  acc_mngr.broker_accounts_.emplace(b->id, b);
  CreateSubAccount(name.empty() ? "test" : name.c_str(), b);
  MarketDataManager::Instance().AddAdapter(sim);
  ExchangeConnectivityManager::Instance().AddAdapter(sim);
}

SubAccount* Backtest::CreateSubAccount(const std::string& name,
                                       const BrokerAccount* broker) {
  auto& acc_mngr = AccountManager::Instance();
  auto s = new SubAccount();
  s->name = strdup(name.c_str());
  auto broker_accs = boost::make_shared<SubAccount::BrokerAccountMap>();
  broker_accs->emplace(0, broker ? broker : acc_mngr.GetBrokerAccount(0));
  s->set_broker_accounts(broker_accs);
  s->id = acc_mngr.sub_accounts_.size();
  acc_mngr.sub_accounts_.emplace(s->id, s);
  acc_mngr.sub_account_of_name_.emplace(s->name, s);
  auto user = const_cast<User*>(acc_mngr.GetUser(0));
  auto sub_accs =
      boost::make_shared<User::SubAccountMap>(*user->sub_accounts().get());
  sub_accs->emplace(s->id, s);
  user->set_sub_accounts(sub_accs);
  return s;
}

void Backtest::Start(const std::string& py,
                     const std::string& default_tick_file) {
  obj_ = bp::object(bp::ptr(this));

  for (auto& pair : SecurityManager::Instance().securities()) {
    pair.second->close_price = 0;
  }
  auto& acc_mngr = AccountManager::Instance();
  auto u = new User();
  u->name = "backtest";
  acc_mngr.users_.emplace(u->id, u);
  acc_mngr.user_of_name_.emplace(u->name, u);

  bp::import("sys").attr("path").attr("insert")(
      0, fs::path(py).parent_path().string());
  auto fn = fs::path(py).filename().string();
  auto module_name = fn.substr(0, fn.length() - 3);
  bp::object m;
  try {
    m = bp::import(module_name.c_str());
  } catch (const bp::error_already_set& err) {
    PrintPyError("load python", true);
    return;
  }
  LOG_INFO(module_name + " loaded");
  if (simulators_.empty()) AddSimulator(default_tick_file);
  on_start_ = GetCallable(m, "on_start");
  on_start_of_day_ = GetCallable(m, "on_start_of_day");
  on_end_ = GetCallable(m, "on_end");
  on_end_of_day_ = GetCallable(m, "on_end_of_day");
  if (!on_start_) return;
  try {
    on_start_(obj_);
  } catch (const bp::error_already_set& err) {
    PrintPyError("on_start", true);
  }

  auto trade_hit_ratio_str = getenv("TRADE_HIT_RATIO");
  if (trade_hit_ratio_str) {
    trade_hit_ratio_ = atof(trade_hit_ratio_str);
  }
  LOG_INFO("TRADE_HIT_RATIO=" << trade_hit_ratio_);

  auto latency_str = getenv("LATENCY");
  if (latency_str) {
    latency_ = atof(latency_str);
  }
  LOG_INFO("LATENCY=" << latency_);

  auto used_symbols_str = getenv("USED_SYMBOLS");
  if (used_symbols_str) {
    for (auto& str : Split(used_symbols_str, ",")) used_symbols_.insert(str);
  }
}

void Backtest::End() {
  if (on_end_) {
    try {
      on_end_(obj_);
    } catch (const bp::error_already_set& err) {
      PrintPyError("on_end", true);
    }
  }
  of_.close();
}

}  // namespace opentrade

#endif  // BACKTEST
