#include "python.h"

#include <Python.h>
#include <boost/filesystem.hpp>

#include "backtest.h"
#include "bar_handler.h"
#include "logger.h"
#include "server.h"

namespace fs = boost::filesystem;

namespace opentrade {

struct LockGIL {
  explicit LockGIL(const std::string &token = "") {
    m.lock();
    test_token_saved = test_token;
    test_token = token;
  }
  ~LockGIL() {
    test_token = test_token_saved;
    m.unlock();
  }
  static inline std::recursive_mutex
      m;  // happens in calling Algo::Stop, to-do: will remove
  static inline std::string test_token;
  static inline std::string test_token_saved;
};

template <typename T>
static inline bool GetValueScalar(const bp::object &value, T *out) {
  auto ptr = value.ptr();
  if (PyFloat_Check(ptr)) {
    *out = PyFloat_AsDouble(ptr);
  } else if (PyLong_Check(ptr)) {
    *out = PyLong_AsLong(ptr);
#if PY_MAJOR_VERSION < 3
  } else if (PyInt_Check(ptr)) {
    *out = PyInt_AsLong(ptr);
#endif
  } else if (ptr == Py_True) {
    *out = true;
  } else if (ptr == Py_False) {
    *out = false;
  } else {
    try {
      *out = bp::extract<std::string>(value);
      return true;
    } catch (const bp::error_already_set &err) {
      PyErr_Clear();
      try {
        *out = bp::extract<SecurityTuple &>(value);
        return true;
      } catch (const bp::error_already_set &err) {
        PyErr_Clear();
        return false;
      }
    }
  }
  return true;
}

#ifdef BACKTEST
#define LOCK() \
  do {         \
  } while (false)
#else
#define LOCK() LockGIL lock(test_token_)
#endif

static std::string Args2Str(bp::tuple args) {
  auto n = bp::len(args);
  std::stringstream ss;
  for (auto i = 0u; i < n; ++i) {
    if (i > 0) ss << ' ';
    ss << bp::extract<const char *>(bp::str(args[i]));
  }
  return ss.str();
}

template <typename T>
struct ContainerWrapper {
  explicit ContainerWrapper(T v) : ptr(v) {}
  decltype(auto) len() { return ptr->size(); }
  decltype(auto) begin() { return ptr->begin(); }
  decltype(auto) end() { return ptr->end(); }
  T ptr;
};

typedef ContainerWrapper<const Instrument::Orders *> OrdersWrapper;
typedef std::vector<const Security *> Securities;
typedef std::shared_ptr<Securities> SecuritiesPtr;
typedef ContainerWrapper<SecuritiesPtr> SecuritiesWrapper;

#define PUBLISH_TEST_MSG(type, msg)                        \
  if (LockGIL::test_token.size()) {                        \
    std::stringstream os;                                  \
    os << type << " - " << msg;                            \
    Server::PublishTestMsg(LockGIL::test_token, os.str()); \
  }
#define LOG2_DEBUG(msg)           \
  PUBLISH_TEST_MSG("DEBUG", msg); \
  LOG_DEBUG(msg)
#define LOG2_INFO(msg)           \
  PUBLISH_TEST_MSG("INFO", msg); \
  LOG_INFO(msg)
#define LOG2_WARN(msg)           \
  PUBLISH_TEST_MSG("WARN", msg); \
  LOG_WARN(msg)
#define LOG2_ERROR(msg)           \
  PUBLISH_TEST_MSG("ERROR", msg); \
  LOG_ERROR(msg)
#define LOG2_FATAL(msg)           \
  PUBLISH_TEST_MSG("FATAL", msg); \
  LOG_FATAL(msg)

BOOST_PYTHON_MODULE(opentrade) {
  bp::enum_<OrderSide>("OrderSide")
      .value("buy", kBuy)
      .value("sell", kSell)
      .value("short", kShort);

  bp::enum_<OrderType>("OrderType")
      .value("market", kMarket)
      .value("limit", kLimit)
      .value("stop", kStop)
      .value("stop_limit", kStopLimit)
      .value("otc", kOTC);

  bp::enum_<ExecTransType>("ExecTransType")
      .value("new", kTransNew)
      .value("cancel", kTransCancel)
      .value("correct", kTransCorrect)
      .value("status", kTransStatus);

  bp::enum_<OrderStatus>("OrderStatus")
      .value("new", kNew)
      .value("partially_filled", kPartiallyFilled)
      .value("filled", kFilled)
      .value("done_for_day", kDoneForDay)
      .value("canceled", kCanceled)
      .value("replace", kReplaced)
      .value("pending_cancel", kPendingCancel)
      .value("stopped", kStopped)
      .value("rejected", kRejected)
      .value("suspended", kSuspended)
      .value("pending_new", kPendingNew)
      .value("calculated", kCalculated)
      .value("expired", kExpired)
      .value("accept_for_bidding", kAcceptedForBidding)
      .value("pending_replace", kPendingReplace)
      .value("risk_rejected", kRiskRejected)
      .value("unconfirmed_new", kUnconfirmedNew)
      .value("unconfirmed_cancel", kUnconfirmedCancel)
      .value("unconfirmed_replace", kUnconfirmedReplace)
      .value("cancel_rejected", kCancelRejected);

  bp::enum_<TimeInForce>("TimeInForce")
      .value("day", kDay)
      .value("gtc", kGoodTillCancel)
      .value("opg", kAtTheOpening)
      .value("ioc", kImmediateOrCancel)
      .value("fok", kFillOrKill)
      .value("gtx", kGoodTillCrossing)
      .value("gtd", kGoodTillDate);

  bp::class_<DataSrc>("DataSrc", bp::init<const char *>())
      .def("__repr__", &DataSrc::str);

  bp::class_<SubAccount, boost::noncopyable>("SubAccount", bp::no_init)
      .def("__repr__",
           +[](const SubAccount &acc) { return std::string(acc.name); })
      .add_property(
          "positions",
          +[](const SubAccount &acc) {
            bp::list out;
            for (auto &pair : PositionManager::Instance().sub_positions()) {
              if (pair.first.first != acc.id) continue;
              auto sec = SecurityManager::Instance().Get(pair.first.second);
              out.append(bp::make_tuple(bp::object(bp::ptr(sec)),
                                        bp::object(bp::ptr(&pair.second))));
            }
            return out;
          })
      .def_readonly("id", &SubAccount::id)
      .def_readonly("name", &SubAccount::name);

  bp::class_<User, boost::noncopyable>("User", bp::no_init)
      .def("__repr__", +[](const User &user) { return std::string(user.name); })
      .add_property(
          "positions",
          +[](const User &user) {
            bp::list out;
            for (auto &pair : PositionManager::Instance().user_positions()) {
              if (pair.first.first != user.id) continue;
              auto sec = SecurityManager::Instance().Get(pair.first.second);
              out.append(bp::make_tuple(bp::object(bp::ptr(sec)),
                                        bp::object(bp::ptr(&pair.second))));
            }
            return out;
          })
      .def_readonly("id", &User::id)
      .def_readonly("name", &User::name);

  bp::class_<Exchange, boost::noncopyable>("Exchange", bp::no_init)
      .def("__repr__", +[](const Exchange &ex) { return std::string(ex.name); })
      .def_readonly("name", &Exchange::name)
      .def_readonly("mic", &Exchange::mic)
      .def_readonly("bb_name", &Exchange::bb_name)
      .def_readonly("ib_name", &Exchange::ib_name)
      .def_readonly("tz", &Exchange::tz)
      .def_readonly("trade_start", &Exchange::trade_start)
      .add_property("trade_end", &Exchange::trade_end)
      .def_readonly("break_start", &Exchange::break_start)
      .def_readonly("break_end", &Exchange::break_end)
      .def_readonly("utc_time_offset", &Exchange::utc_time_offset)
      .def_readonly("country", &Exchange::country)
      .def_readonly("odd_lot_allowed", &Exchange::odd_lot_allowed)
      .def("get_security", &Exchange::Get, bp::return_internal_reference<>())
      .add_property("date", &Exchange::GetDate)
      .add_property("seconds", &Exchange::GetSeconds)
      .add_property("securities", +[](const Exchange &self) {
        auto tmp = new Securities;
        tmp->reserve(self.security_of_name.size());
        for (auto &pair : self.security_of_name) {
          tmp->push_back(pair.second);
        }
        return SecuritiesWrapper(SecuritiesPtr(tmp));
      });

  bp::class_<Position>("Position", bp::no_init)
      .def("__repr__",
           +[](const Position &p) {
             std::stringstream ss;
             ss << "Position(qty=" << p.qty << ", avg_px=" << p.avg_px
                << ", total_bought_qty=" << p.total_bought_qty
                << ", total_sold_qty=" << p.total_sold_qty
                << ", total_bought=" << p.total_bought
                << ", total_sold=" << p.total_sold
                << ", total_outstanding_buy_qty=" << p.total_outstanding_buy_qty
                << ", total_outstanding_sell_qty="
                << p.total_outstanding_sell_qty
                << ", unrealized_pnl=" << p.unrealized_pnl
                << ", commission=" << p.commission
                << ", realized_pnl=" << p.realized_pnl << ")";
             return ss.str();
           })
      .def_readonly("qty", &Position::qty)
      .def_readonly("cx_qty", &Position::cx_qty)
      .def_readonly("avg_px", &Position::avg_px)
      .def_readonly("unrealized_pnl", &Position::unrealized_pnl)
      .def_readonly("commission", &Position::commission)
      .def_readonly("realized_pnl", &Position::realized_pnl)
      .def_readonly("total_bought_qty", &Position::total_bought_qty)
      .def_readonly("total_sold_qty", &Position::total_sold_qty)
      .def_readonly("total_bought", &Position::total_bought)
      .def_readonly("total_sold", &Position::total_sold)
      .def_readonly("total_outstanding_buy_qty",
                    &Position::total_outstanding_buy_qty)
      .def_readonly("total_outstanding_sell_qty",
                    &Position::total_outstanding_sell_qty);

  auto cls = bp::class_<Security, boost::noncopyable>("Security", bp::no_init);
  cls.def_readonly("id", &Security::id)
      .def("__repr__",
           +[](const Security &s) {
             std::stringstream ss;
             ss << "Security(symbol=" << s.symbol
                << ", exchange=" << (s.exchange ? s.exchange->name : "") << ")";
             return ss.str();
           })
      .def_readonly("symbol", &Security::symbol)
      .def_readonly("isin", &Security::isin)
      .def_readonly("cusip", &Security::cusip)
      .def_readonly("sedol", &Security::sedol)
      .def_readonly("ric", &Security::ric)
      .def_readonly("bbgid", &Security::bbgid)
      .def_readonly("currency", &Security::currency)
      .def_readonly("rate", &Security::rate)
      .def_readonly("adv20", &Security::adv20)
      .def_readonly("market_cap", &Security::market_cap)
      .def_readonly("sector", &Security::sector)
      .def_readonly("industry_group", &Security::industry_group)
      .def_readonly("industry", &Security::industry)
      .def_readonly("sub_industry", &Security::sub_industry)
      .def_readonly("strike_price", &Security::strike_price)
      .def_readonly("maturity_date", &Security::maturity_date)
      .def_readonly("put_or_call", &Security::put_or_call)
      .def_readonly("opt_attribute", &Security::opt_attribute)
      .def_readonly("multiplier", &Security::multiplier)
      .def_readonly("lot_size", &Security::lot_size)
      .def("get_tick_size", &Security::GetTickSize)
      .add_property("md", bp::make_function(
                              +[](const Security &sec) {
                                return &MarketDataManager::Instance().Get(sec);
                              },
                              bp::return_internal_reference<>()))
      .def_readonly("type", &Security::type)
      .add_property(
          "exchange",
          bp::make_function(+[](const Security &s) { return s.exchange; },
                            bp::return_internal_reference<>()))
      .add_property(
          "underlying",
          bp::make_function(+[](const Security &s) { return s.underlying; },
                            bp::return_internal_reference<>()))
      .def("get_position",
           +[](const Security &sec, const SubAccount *acc) {
             return acc ? &PositionManager::Instance().Get(*acc, sec) : nullptr;
           },
           bp::return_internal_reference<>())
      .def("get_broker_position",
           +[](const Security &sec, const SubAccount *acc) {
             if (!acc) return (const Position *)nullptr;
             auto broker = acc->GetBrokerAccount(sec.exchange->id);
             if (!broker) return (const Position *)nullptr;
             return acc ? &PositionManager::Instance().Get(*broker, sec)
                        : nullptr;
           },
           bp::return_internal_reference<>())
      .def("get_user_position",
           +[](const Security &sec, const User *user) {
             if (!user) return (const Position *)nullptr;
             return user ? &PositionManager::Instance().Get(*user, sec)
                         : nullptr;
           },
           bp::return_internal_reference<>())
#ifdef BACKTEST
      .def("set_adj",
           +[](Security &sec, bp::object adjs) {
             try {
               auto n = bp::len(adjs);
               for (auto i = 0u; i < n; ++i) {
                 auto x = adjs[i];
                 sec.adjs.emplace_back(bp::extract<size_t>(x[0]),
                                       bp::extract<double>(x[1]),
                                       bp::extract<double>(x[2]));
               }
               std::sort(sec.adjs.begin(), sec.adjs.end());
             } catch (const bp::error_already_set &err) {
               PrintPyError("set_adj", true);
             }
           })
#endif
      .add_property("is_in_trade_period", &Security::IsInTradePeriod)
      .def_readonly("local_symbol", &Security::local_symbol);

  bp::class_<SecurityTuple>("SecurityTuple")
      .def("__repr__",
           +[](const SecurityTuple &st) {
             std::stringstream ss;
             ss << "SecurityTuple(src=" << st.src.str() << ", side="
                << bp::extract<const char *>(
                       bp::str(bp::object(bp::ptr(&st)).attr("side")))
                << ", qty=" << st.qty << ", sec=("
                << bp::extract<const char *>(
                       bp::str(bp::object(bp::ptr(&st)).attr("sec")))
                << ")"
                << ", acc=" << (st.acc ? st.acc->name : "") << ")";
             return ss.str();
           })
      .def_readwrite("src", &SecurityTuple::src)
      .def_readwrite("side", &SecurityTuple::side)
      .def_readwrite("qty", &SecurityTuple::qty)
      .add_property(
          "sec",
          bp::make_function(+[](const SecurityTuple &st) { return st.sec; },
                            bp::return_internal_reference<>()),
          +[](SecurityTuple &st, const Security *sec) { st.sec = sec; })
      .add_property(
          "acc",
          bp::make_function(+[](const SecurityTuple &st) { return st.acc; },
                            bp::return_internal_reference<>()),
          +[](SecurityTuple &st, const SubAccount *acc) { st.acc = acc; });

  bp::class_<Contract>("Contract")
      .add_property("is_buy", &Contract::IsBuy)
      .add_property("sec",
                    bp::make_function(+[](const Contract &c) { return c.sec; },
                                      bp::return_internal_reference<>()))
      .add_property("acc",
                    bp::make_function(+[](const Contract &c) { return c.acc; },
                                      bp::return_internal_reference<>()),
                    +[](Contract &c, const SubAccount *acc) { c.acc = acc; })
      .def_readwrite("qty", &Contract::qty)
      .def_readwrite("price", &Contract::price)
      .def_readwrite("stop_price", &Contract::stop_price)
      .def_readwrite("side", &Contract::side)
      .def_readwrite("tif", &Contract::tif)
      .def_readwrite("type", &Contract::type);

  bp::class_<Bar>("Bar", bp::no_init)
      .def_readonly("tm", &Bar::tm)
      .def_readonly("open", &MarketData::Trade::open)
      .def_readonly("high", &MarketData::Trade::high)
      .def_readonly("low", &MarketData::Trade::low)
      .def_readonly("close", &MarketData::Trade::close)
      .def_readonly("qty", &MarketData::Trade::qty)
      .def_readonly("volume", &MarketData::Trade::volume)
      .def_readonly("vwap", &MarketData::Trade::vwap)
      .def("__repr__", +[](const Bar &t) {
        std::stringstream ss;
        ss << "Bar(tm=" << t.tm << ", open=" << t.open << ", high=" << t.high
           << ", low=" << t.low << ", close=" << t.close << ", qty=" << t.qty
           << ", volume=" << t.volume << ", vwap=" << t.vwap << ")";
        return ss.str();
      });

  bp::class_<MarketData>("MarketData", bp::no_init)
      .def_readonly("tm", &MarketData::tm)
      .add_property("open", +[](const MarketData &md) { return md.trade.open; })
      .add_property("high", +[](const MarketData &md) { return md.trade.high; })
      .add_property("low", +[](const MarketData &md) { return md.trade.low; })
      .add_property("close",
                    +[](const MarketData &md) { return md.trade.close; })
      .add_property("qty", +[](const MarketData &md) { return md.trade.qty; })
      .add_property("vwap", +[](const MarketData &md) { return md.trade.vwap; })
      .add_property("volume",
                    +[](const MarketData &md) { return md.trade.volume; })
      .add_property("ask_price",
                    +[](const MarketData &md) { return md.quote().ask_price; })
      .add_property("bid_price",
                    +[](const MarketData &md) { return md.quote().bid_price; })
      .add_property("ask_size",
                    +[](const MarketData &md) { return md.quote().ask_size; })
      .add_property("bid_size",
                    +[](const MarketData &md) { return md.quote().bid_size; })
      .def("get_ask_price",
           +[](const MarketData &md, size_t i) {
             return md.depth[std::min(i, MarketData::kDepthSize - 1)].ask_price;
           })
      .def("get_bid_price",
           +[](const MarketData &md, size_t i) {
             return md.depth[std::min(i, MarketData::kDepthSize - 1)].bid_price;
           })
      .def("get_ask_size",
           +[](const MarketData &md, size_t i) {
             return md.depth[std::min(i, MarketData::kDepthSize - 1)].ask_size;
           })
      .def("get_bid_size", +[](const MarketData &md, size_t i) {
        return md.depth[std::min(i, MarketData::kDepthSize - 1)].bid_size;
      });

  bp::class_<Confirmation>("Confirmation", bp::no_init)
      .add_property("order", bp::make_function(
                                 +[](const Confirmation &c) { return c.order; },
                                 bp::return_internal_reference<>()))
      .def_readonly("exec_id", &Confirmation::exec_id)
      .def_readonly("transaction_time", &Confirmation::transaction_time)
      .def_readonly("order_id", &Confirmation::order_id)
      .def_readonly("text", &Confirmation::text)
      .def_readonly("exec_type", &Confirmation::exec_type)
      .def_readonly("exec_trans_type", &Confirmation::exec_trans_type)
      .add_property(
          "last_shares",
          +[](const Confirmation &c) {
            if (c.exec_type == kFilled || c.exec_type == kPartiallyFilled)
              return c.last_shares;
            return 0.;
          })
      .def_readonly("last_px", &Confirmation::last_px);

  bp::class_<Order, bp::bases<Contract>>("Order", bp::no_init)
      .add_property("instrument",
                    bp::make_function(+[](const Order &o) { return o.inst; },
                                      bp::return_internal_reference<>()))
      .def_readonly("status", &Order::status)
      .def_readonly("id", &Order::id)
      .def_readonly("orig_id", &Order::orig_id)
      .def_readonly("avg_px", &Order::avg_px)
      .def_readonly("cum_qty", &Order::cum_qty)
      .def_readonly("leaves_qty", &Order::leaves_qty)
      .add_property("is_live", &Order::IsLive);

  bp::class_<OrdersWrapper>("OrdersWrapper", bp::no_init)
      .def("__len__", &OrdersWrapper::len)
      .def("__iter__", bp::range<bp::return_internal_reference<>>(
                           &OrdersWrapper::begin, &OrdersWrapper::end));

  bp::class_<SecuritiesWrapper>("SecuritiesWrapper", bp::no_init)
      .def("__len__", &SecuritiesWrapper::len)
      .def("__iter__", bp::range<bp::return_internal_reference<>>(
                           &SecuritiesWrapper::begin, &SecuritiesWrapper::end));

  bp::class_<Instrument>("Instrument", bp::no_init)
      .add_property("sec", bp::make_function(&Instrument::sec,
                                             bp::return_internal_reference<>()))
      .add_property("md", bp::make_function(&Instrument::md,
                                            bp::return_internal_reference<>()))
      .add_property("bought_qty", &Instrument::bought_qty)
      .add_property("bought_qty", &Instrument::bought_qty)
      .add_property("sold_qty", &Instrument::sold_qty)
      .add_property("outstanding_buy_qty", &Instrument::outstanding_buy_qty)
      .add_property("outstanding_sell_qty", &Instrument::outstanding_sell_qty)
      .add_property("net_outstanding_qty", &Instrument::net_outstanding_qty)
      .add_property("total_outstanding_qty", &Instrument::total_outstanding_qty)
      .add_property("total_exposure", &Instrument::total_exposure)
      .add_property("net_qty", &Instrument::net_qty)
      .add_property("net_cx_qty", &Instrument::net_cx_qty)
      .add_property("total_qty", &Instrument::total_qty)
      .add_property("total_cx_qty", &Instrument::total_cx_qty)
      .add_property("id", &Instrument::id)
      .def("unlisten", &Instrument::UnListen)
      .def("subscribe", &Instrument::SubscribeByName,
           (bp::arg("self"), bp::arg("indicator_name"),
            bp::arg("listen") = false))
      .def("get",
           +[](Instrument &inst, Indicator::IdType indicator_id) {
             auto ind = inst.Get(indicator_id);
             if (ind) return ind->GetPyObject();
             return bp::object{};
           })
      .add_property("active_orders", +[](const Instrument &inst) {
        return OrdersWrapper(&inst.active_orders());
      });

  bp::class_<Python>("Algo", bp::no_init)
      .def("subscribe", &Python::Subscribe,
           (bp::arg("self"), bp::arg("sec"), bp::arg("src") = DataSrc{},
            bp::arg("listen") = true),
           bp::return_internal_reference<>())
      .def("place", &Python::Place, bp::return_internal_reference<>())
      .def("cancel",
           +[](Python &algo, const Order *ord) {
             if (ord) return algo.Cancel(*ord);
             return false;
           })
      .def("stop", &Python::Stop)
      .def("cross", &Python::Cross)
      .def("set_timeout", &Python::SetTimeout)
      .add_property("user",
                    bp::make_function(+[](Algo &algo) { return &algo.user(); },
                                      bp::return_internal_reference<>()))
      .add_property("id", &Algo::id)
      .add_property("name", +[](const Python &algo) { return algo.name(); })
      .add_property("is_active", &Algo::is_active);

  bp::def("get_security",
          bp::make_function(
              +[](Security::IdType id) {
                return SecurityManager::Instance().Get(id);
              },
              bp::return_value_policy<bp::reference_existing_object>()));

  bp::def("get_exchange",
          bp::make_function(
              +[](const std::string &name) {
                return SecurityManager::Instance().GetExchange(name);
              },
              bp::return_value_policy<bp::reference_existing_object>()));

#ifdef BACKTEST
  bp::def("add_simulator", bp::make_function(+[](const std::string &fn_tmpl,
                                                 const std::string &name) {
            Backtest::Instance().AddSimulator(fn_tmpl, name);
          }));
#endif

  bp::def("get_account",
          bp::make_function(
              +[](const std::string &name) {
                auto acc = AccountManager::Instance().GetSubAccount(name);
#ifdef BACKTEST
                if (!acc) acc = Backtest::Instance().CreateSubAccount(name);
#endif
                return acc;
              },
              bp::return_value_policy<bp::reference_existing_object>()));

  bp::def("log_debug", bp::raw_function(
                           +[](bp::tuple args, bp::dict kwargs) {
                             LOG2_DEBUG(Args2Str(args));
                             return bp::object{};
                           },
                           0));
  bp::def("log_info", bp::raw_function(
                          +[](bp::tuple args, bp::dict kwargs) {
                            LOG2_INFO(Args2Str(args));
                            return bp::object{};
                          },
                          0));
  bp::def("log_warn", bp::raw_function(
                          +[](bp::tuple args, bp::dict kwargs) {
                            LOG2_WARN(Args2Str(args));
                            return bp::object{};
                          },
                          0));
  bp::def("log_error", bp::raw_function(
                           +[](bp::tuple args, bp::dict kwargs) {
                             LOG2_ERROR(Args2Str(args));
                             return bp::object{};
                           },
                           0));

  bp::def("get_time",
          +[]() { return NowUtcInMicro() / static_cast<double>(kMicroInSec); });
  bp::def("get_datetime", +[]() {
    return bp::import("datetime")
        .attr("datetime")
        .attr("fromtimestamp")(NowUtcInMicro() /
                               static_cast<double>(kMicroInSec));
  });

  bp::def("get_exchanges", +[]() {
    bp::list out;
    for (auto &pair : SecurityManager::Instance().exchanges()) {
      out.append(bp::object(bp::ptr(pair.second)));
    }
    return out;
  });

#ifdef BACKTEST
  bp::class_<Backtest, boost::noncopyable>("Backtest", bp::no_init)
      .def("clear",
           +[](Backtest &) {
             // clear is called always, not clearing cause a lot of problems
             LOG2_WARN("backtest clear is deprecated");
           })
      .def("skip", &Backtest::Skip)
      .def("set_timeout",
           +[](Backtest &, bp::object func, double seconds) {
             if (seconds < 0) seconds = 0;
             kTimers.emplace(kTime + seconds * kMicroInSec, [func]() {
               try {
                 func();
               } catch (const bp::error_already_set &err) {
                 PrintPyError("set_timeout");
               }
             });
           })
      .def("cancel_algo", bp::make_function(+[](Backtest &, const Security &sec,
                                                const SubAccount &acc) {
             AlgoManager::Instance().Stop(sec.id, acc.id);
           }))
      .add_property("user", bp::make_function(
                                +[](Backtest &) {
                                  return AccountManager::Instance().GetUser(0);
                                },
                                bp::return_internal_reference<>()))
      .def("start_algo",
           bp::make_function(+[](Backtest &, const std::string &name,
                                 bp::dict params) {
             auto user = AccountManager::Instance().GetUser(0);
             auto params_ptr = std::make_shared<Algo::ParamMap>();
             auto items = params.items();
             for (auto i = 0u; i < bp::len(items); ++i) {
               std::string key = bp::extract<std::string>(items[i][0]);
               ParamDef::Value value;
               if (!GetValueScalar(items[i][1], &value)) {
                 LOG_ERROR("Invalid '"
                           << key << "' value: "
                           << bp::extract<const char *>(bp::str(items[i][1])));
               } else {
                 (*params_ptr)[key] = value;
               }
             }
             for (auto &pair : *params_ptr) {
               if (auto pval = std::get_if<SecurityTuple>(&pair.second)) {
                 if (!pval->acc) {
                   pval->acc = AccountManager::Instance().GetSubAccount(0);
                   params[pair.first].attr("acc") =
                       bp::object(bp::ptr(pval->acc));
                 }
               }
             }
             auto algo =
                 AlgoManager::Instance().Spawn(params_ptr, name, *user, "", "");
             if (!algo) {
               LOG_ERROR("Unknown algo name: " << name);
             }
             return algo ? algo->id() : 0;
           }));
#endif
}

#if PY_MAJOR_VERSION >= 3
#define INIT_MODULE PyInit_opentrade
extern "C" PyObject *INIT_MODULE();
#else
#define INIT_MODULE initopentrade
extern "C" void INIT_MODULE();
#endif

void PrintPyError(const char *from, bool fatal) {
  PyObject *ptype, *pvalue, *ptraceback;
  PyErr_Fetch(&ptype, &pvalue, &ptraceback);
  PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
  bp::object type(bp::handle<>(bp::borrowed(ptype)));
  bp::object value(bp::handle<>(bp::borrowed(pvalue)));
  std::string result;
  if (ptraceback) {
    bp::object tb(bp::handle<>(bp::borrowed(ptraceback)));
    if (ptraceback) {
      bp::object lines =
          bp::import("traceback").attr("format_exception")(type, value, tb);
      for (auto i = 0; i < bp::len(lines); ++i)
        result += bp::extract<std::string>(lines[i])();
    }
  } else {
    std::string errstr = bp::extract<std::string>(bp::str(value));
    std::string typestr =
        bp::extract<std::string>(bp::str(type.attr("__name__")));
    try {
      std::string text = bp::extract<std::string>(value.attr("text"));
      int64_t offset = bp::extract<int64_t>(value.attr("offset"));
      char space[offset + 1] = {};
      for (auto i = 0; i < offset - 1; ++i) space[i] = ' ';
      errstr += "\n" + text + space + "^";
    } catch (const bp::error_already_set &err) {
      PyErr_Clear();
    }
    result = typestr + ": " + errstr;
  }
  PyErr_Restore(ptype, pvalue, ptraceback);
  PyErr_Clear();
  if (fatal) {
    LOG2_FATAL(from << "\n" << result);
  } else {
    LOG2_ERROR(from << "\n" << result);
  }
}

static inline double GetDouble(const bp::object &obj) {
  auto ptr = obj.ptr();
  if (PyFloat_Check(ptr)) return PyFloat_AsDouble(ptr);
  if (PyLong_Check(ptr)) return PyLong_AsLong(ptr);
#if PY_MAJOR_VERSION < 3
  if (PyInt_Check(ptr)) return PyInt_AsLong(ptr);
#endif
  return 0;
}

bp::object GetCallable(const bp::object &m, const char *name) {
  if (!PyObject_HasAttrString(m.ptr(), name)) return {};
  bp::object func = m.attr(name);
  if (!PyCallable_Check(func.ptr())) {
    return {};
  }
  LOG2_INFO("Loaded python function " << name);
  return func;
}

void InitalizePy() {
  PyImport_AppendInittab(const_cast<char *>("opentrade"), INIT_MODULE);
  Py_InitializeEx(0);  // no signal registration
  if (!PyEval_ThreadsInitialized()) PyEval_InitThreads();
  LockGIL lock;
  bp::import("sys").attr("path").attr("insert")(0, "./algos");
  kOpentrade = bp::import("opentrade");
  LOG2_INFO("Python initialized");
}

PyModule LoadPyModule(const std::string &module_name) {
  bp::object m;
  try {
    m = bp::import(module_name.c_str());
  } catch (const bp::error_already_set &err) {
    PrintPyError("load python");
    return {};
  }
  LOG2_INFO(module_name + " loaded");
  auto func = GetCallable(m, "get_param_defs");
  if (!func.ptr()) {
    LOG2_ERROR("Can not find function \"get_param_defs\" in " << module_name);
    return {};
  }
  PyModule out;
  out.get_param_defs = func;
  out.test = GetCallable(m, "test");
  out.on_start = GetCallable(m, "on_start");
  out.on_modify = GetCallable(m, "on_modify");
  out.on_stop = GetCallable(m, "on_stop");
  out.on_market_trade = GetCallable(m, "on_market_trade");
  out.on_market_quote = GetCallable(m, "on_market_quote");
  out.on_confirmation = GetCallable(m, "on_confirmation");
  out.on_indicator = GetCallable(m, "on_indicator");
  return out;
}

static inline bool ParseParamDef(const bp::object &item, ParamDef *out) {
  // (name, default_value, required, min_value, max_value, precision)
  ParamDef::ValueVector value_vector;
  auto n2 = PyTuple_Size(item.ptr());
  if (n2 < 2) return false;
  bp::object tmp;
  tmp = item[0];
  out->name = bp::extract<std::string>(tmp);
  bp::object value = item[1];
  if (PyTuple_Check(value.ptr()) || PyList_Check(value.ptr())) {
    auto n3 = PyTuple_Size(value.ptr());
    value_vector.resize(n3);
    for (auto i = 0; i < n3; ++i) {
      tmp = value[i];
      if (!GetValueScalar(tmp, &value_vector[i])) return false;
    }
    out->default_value = value_vector;
  } else {
    if (!GetValueScalar(value, &out->default_value)) return false;
  }
  if (n2 == 2) return true;
  tmp = item[2];
  out->required = PyObject_IsTrue(tmp.ptr());
  if (n2 == 3) return true;
  tmp = item[3];
  out->min_value = GetDouble(tmp);
  if (n2 == 4) return true;
  tmp = item[4];
  out->max_value = GetDouble(tmp);
  if (n2 == 5) return true;
  tmp = item[5];
  out->precision = GetDouble(tmp);
  return true;
}

static ParamDefs ParseParamDefs(const bp::object &func) {
  try {
    auto out = func();
    if (!out || !PyTuple_Check(out.ptr())) {
      LOG2_ERROR("get_param_defs must return tuple");
      return {};
    }
    auto n = PyTuple_Size(out.ptr());
    ParamDefs defs;
    defs.resize(n);
    for (auto i = 0; i < n; ++i) {
      bp::object item = out[i];
      if (!PyTuple_Check(item.ptr()) || !ParseParamDef(item, &defs[i])) {
        LOG2_ERROR("Invalid param definition \""
                   << bp::extract<const char *>(bp::str(item)) << "\"");
        return {};
      }
    }
    return defs;
  } catch (const bp::error_already_set &err) {
    PrintPyError("parse param defs");
    return {};
  }
}

static bp::dict CreateParamsDict(const Algo::ParamMap &params) {
  bp::dict obj;
  for (auto &pair : params) {
    if (auto pval = std::get_if<bool>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<int32_t>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<int64_t>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<const char *>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<std::string>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<double>(&pair.second)) {
      obj[pair.first] = *pval;
    } else if (auto pval = std::get_if<SecurityTuple>(&pair.second)) {
      obj[pair.first] = *pval;
    }
  }
  return obj;
}

Python *Python::LoadModule(const std::string &module_name) {
  auto p = new Python;
  auto m = LoadPyModule(module_name);
  if (!m.get_param_defs) return nullptr;
  auto def = ParseParamDefs(m.get_param_defs);
  if (def.empty()) {
    delete p;
    return nullptr;
  }
  p->py_ = m;
  p->def_ = std::move(def);
  return p;
}

Python *Python::Load(const std::string &module_name) {
  LockGIL lock;
  auto p = LoadModule(module_name);
  if (!p) return p;
  p->create_func_ = [p]() {
    auto p2 = new Python;
    LockGIL lock;
    p2->py_ = p->py_;
    p2->obj_ = bp::object(bp::ptr(p2));
    return p2;
  };
  return p;
}

Python *Python::LoadTest(const std::string &module_name,
                         const std::string &token) {
  LockGIL lock(token);
  LOG2_DEBUG("test token " << token);
  auto fn = kAlgoPath / (module_name + ".py");
  auto module_name2 = "_" + module_name + "_" + token;
  auto fn2 = kAlgoPath / (module_name2 + ".py");
  try {
    fs::copy_file(fn, fn2, fs::copy_option::overwrite_if_exists);
  } catch (const fs::filesystem_error &err) {
    LOG2_ERROR(err.what());
    return nullptr;
  }
  auto p = LoadModule(module_name2);
  try {
    fs::remove(fn2);
  } catch (const fs::filesystem_error &err) {
  }
  if (p) {
    p->set_name(module_name);
    p->test_token_ = token;
    p->obj_ = bp::object(bp::ptr(p));
  } else {
    Server::PublishTestMsg(token, "test " + token + " done", true);
  }
  return p;
}

void Python::SetTimeout(bp::object func, double seconds) {
  Algo::SetTimeout(
      [this, func]() {
        LOCK();
        try {
          func();
        } catch (const bp::error_already_set &err) {
          PrintPyError("set_timeout");
        }
      },
      seconds);
}

std::string Python::Test() noexcept {
  LOCK();
  if (!py_.test) {
    auto msg = "python function \"test\" is required for running test";
    LOG2_ERROR(msg);
    return msg;
  }
  try {
    auto params = py_.test(obj_);
    if (py_.on_start) {
      auto out = py_.on_start(obj_, params);
      try {
        return bp::extract<std::string>(out);
      } catch (const bp::error_already_set &err) {
        PyErr_Clear();
      }
    }
  } catch (const bp::error_already_set &err) {
    PrintPyError("test");
  }
  return {};
}

std::string Python::OnStart(const ParamMap &params) noexcept {
  if (!py_.on_start) return {};
  LOCK();
  auto tmp = CreateParamsDict(params);
  try {
    auto out = py_.on_start(obj_, tmp);
    try {
      return bp::extract<std::string>(out);
    } catch (const bp::error_already_set &err) {
      PyErr_Clear();
    }
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_start");
    return {};
  }
  return {};
}

void Python::OnModify(const ParamMap &params) noexcept {
  if (!py_.on_modify) return;
  LOCK();
  auto tmp = CreateParamsDict(params);
  try {
    py_.on_modify(obj_, tmp);
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_modify");
  }
}

void Python::OnStop() noexcept {
  if (test_token_.size()) {
    Server::PublishTestMsg(test_token_, "test " + test_token_ + " done", true);
  }
  if (!py_.on_stop) return;
  LOCK();
  try {
    py_.on_stop(obj_);
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_stop");
  }
}

void Python::OnMarketTrade(const Instrument &inst, const MarketData &md,
                           const MarketData &md0) noexcept {
  if (!py_.on_market_trade) return;
  LOCK();
  try {
    py_.on_market_trade(obj_, bp::ptr(&inst));
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_market_trade");
  }
}

void Python::OnMarketQuote(const Instrument &inst, const MarketData &md,
                           const MarketData &md0) noexcept {
  if (!py_.on_market_quote) return;
  LOCK();
  try {
    py_.on_market_quote(obj_, bp::ptr(&inst));
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_market_quote");
  }
}

void Python::OnConfirmation(const Confirmation &cm) noexcept {
  if (!py_.on_confirmation) return;
  LOCK();
  try {
    py_.on_confirmation(obj_, bp::ptr(&cm));
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_confirmation");
  }
}

void Python::OnIndicator(Indicator::IdType id,
                         const Instrument &inst) noexcept {
  if (!py_.on_indicator) return;
  auto ind = inst.Get(id);
  if (!ind) return;
  LOCK();
  try {
    py_.on_indicator(obj_, ind->GetPyObject(), bp::ptr(&inst));
  } catch (const bp::error_already_set &err) {
    PrintPyError("on_indicator");
  }
}

const ParamDefs &Python::GetParamDefs() noexcept { return def_; }

}  // namespace opentrade
