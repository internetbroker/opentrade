#include "sim_server.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "opentrade/market_data.h"

static thread_local boost::uuids::random_generator kUuidGen;
void SimServer::HandleTick(Security::IdType sec, char type, double px,
                           double qty) {
  if (px <= 0 || qty <= 0) return;
  tp_.AddTask([=]() {
    auto size = qty;
    auto& actives = active_orders_[sec];
    if (actives.empty()) return;
    auto it = actives.begin();
    while (it != actives.end() && size > 0) {
      auto& tuple = it->second;
      bool ok;
      switch (type) {
        case 'T':
          ok = (tuple.is_buy && px <= tuple.px) ||
               (!tuple.is_buy && px >= tuple.px);
          break;
        case 'A':
          ok = tuple.is_buy && px <= tuple.px;
          break;
        case 'B':
          ok = !tuple.is_buy && px >= tuple.px;
          break;
        default:
          ok = false;
          break;
      }
      if (!ok) {
        it++;
        continue;
      }
      auto n = std::min(size, tuple.leaves);
      size -= n;
      tuple.leaves -= n;
      assert(size >= 0);
      assert(tuple.leaves >= 0);
      auto& resp = tuple.resp;
      resp.setField(FIX::ExecTransType('0'));
      resp.setField(FIX::ExecType(
          tuple.leaves <= 0 ? FIX::ExecType_FILL : FIX::ExecType_PARTIAL_FILL));
      resp.setField(FIX::OrdStatus(
          tuple.leaves <= 0 ? FIX::ExecType_FILL : FIX::ExecType_PARTIAL_FILL));
      resp.setField(FIX::LastShares(n));
      resp.setField(FIX::LastPx(tuple.px));
      auto eid = boost::uuids::to_string(kUuidGen());
      resp.setField(FIX::ExecID(eid));
      resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
      session_->send(resp);
      if (tuple.leaves <= 0)
        it = actives.erase(it);
      else
        it++;
    }
  });
}

void SimServer::fromApp(const FIX::Message& msg,
                        const FIX::SessionID& session_id) {
  tp_.AddTask(
      [=]() {
        const std::string& msgType =
            msg.getHeader().getField(FIX::FIELD::MsgType);
        auto resp = msg;
        if (msgType == "D") {  // new order
          resp.getHeader().setField(FIX::MsgType("8"));
          auto symbol = msg.getField(FIX::FIELD::Symbol);
          auto exchange = msg.getField(FIX::FIELD::ExDestination);
          auto sec =
              opentrade::SecurityManager::Instance().Get(exchange, symbol);
          if (!sec) {
            resp.setField(FIX::ExecType(FIX::ExecType_REJECTED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_REJECTED));
            resp.setField(FIX::Text("unknown security"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          if (!sec->IsInTradePeriod()) {
            resp.setField(FIX::ExecType(FIX::ExecType_REJECTED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_REJECTED));
            resp.setField(FIX::Text("Not in trading period"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          auto qty = atof(msg.getField(FIX::FIELD::OrderQty).c_str());
          if (qty <= 0) {
            resp.setField(FIX::ExecType(FIX::ExecType_REJECTED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_REJECTED));
            resp.setField(FIX::Text("invalid OrderQty"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          auto px = 0.;
          if (msg.isSetField(FIX::FIELD::Price))
            px = atof(msg.getField(FIX::FIELD::Price).c_str());
          FIX::OrdType type;
          msg.getField(type);
          if (px <= 0 && type != FIX::OrdType_MARKET) {
            resp.setField(FIX::ExecType(FIX::ExecType_REJECTED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_REJECTED));
            resp.setField(FIX::Text("invalid price"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          resp.setField(FIX::ExecType(FIX::ExecType_PENDING_NEW));
          resp.setField(FIX::OrdStatus(FIX::ExecType_PENDING_NEW));
          resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
          session_->send(resp);
          auto clordid = msg.getField(FIX::FIELD::ClOrdID);
          if (used_ids_.find(clordid) != used_ids_.end()) {
            resp.setField(FIX::ExecType(FIX::ExecType_REJECTED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_REJECTED));
            resp.setField(FIX::Text("duplicate ClOrdID"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          used_ids_.insert(clordid);
          resp.setField(FIX::FIELD::OrderID, "SIM-" + clordid);
          resp.setField(FIX::ExecType(FIX::ExecType_NEW));
          resp.setField(FIX::OrdStatus(FIX::ExecType_NEW));
          resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
          session_->send(resp);
          FIX::Side side;
          msg.getField(side);
          auto is_buy = side == FIX::Side_BUY;
          if (type == FIX::OrdType_MARKET) {
            auto q = opentrade::MarketDataManager::Instance().Get(*sec).quote();
            auto qty_q = is_buy ? q.ask_size : q.bid_size;
            auto px_q = is_buy ? q.ask_price : q.bid_price;
            if (!qty_q && sec->type == opentrade::kForexPair) qty_q = 1e9;
            if (qty_q > 0 && px_q > 0) {
              if (qty_q > qty) qty_q = qty;
              resp.setField(FIX::ExecTransType('0'));
              resp.setField(FIX::ExecType(qty_q == qty
                                              ? FIX::ExecType_FILL
                                              : FIX::ExecType_PARTIAL_FILL));
              resp.setField(FIX::OrdStatus(qty_q == qty
                                               ? FIX::ExecType_FILL
                                               : FIX::ExecType_PARTIAL_FILL));
              resp.setField(FIX::LastShares(qty_q));
              resp.setField(FIX::LastPx(px_q));
              auto eid = boost::uuids::to_string(kUuidGen());
              resp.setField(FIX::ExecID(eid));
              resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
              session_->send(resp);
              if (qty_q >= qty) return;
            }
            resp.setField(FIX::ExecType(FIX::ExecType_CANCELLED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_CANCELLED));
            resp.setField(FIX::Text("no quote"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          OrderTuple ord{px, qty, is_buy, resp};
          auto q = opentrade::MarketDataManager::Instance().Get(*sec).quote();
          auto qty_q = is_buy ? q.ask_size : q.bid_size;
          auto px_q = is_buy ? q.ask_price : q.bid_price;
          if (!qty_q && sec->type == opentrade::kForexPair) qty_q = 1e9;
          if (qty_q > 0 && px_q > 0) {
            if ((is_buy && px >= px_q) || (!is_buy && px <= px_q)) {
              if (qty_q > qty) qty_q = qty;
              resp.setField(FIX::ExecTransType('0'));
              resp.setField(FIX::ExecType(qty_q == qty
                                              ? FIX::ExecType_FILL
                                              : FIX::ExecType_PARTIAL_FILL));
              resp.setField(FIX::OrdStatus(qty_q == qty
                                               ? FIX::ExecType_FILL
                                               : FIX::ExecType_PARTIAL_FILL));
              resp.setField(FIX::LastShares(qty_q));
              resp.setField(FIX::LastPx(px_q));
              auto eid = boost::uuids::to_string(kUuidGen());
              resp.setField(FIX::ExecID(eid));
              resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
              session_->send(resp);
              ord.leaves -= qty_q;
              assert(ord.leaves >= 0);
              if (ord.leaves <= 0) return;
            }
          }
          FIX::TimeInForce tif;
          if (msg.isSetField(FIX::FIELD::TimeInForce)) msg.getField(tif);
          if (tif == FIX::TimeInForce_IMMEDIATE_OR_CANCEL) {
            resp.setField(FIX::ExecType(FIX::ExecType_CANCELLED));
            resp.setField(FIX::OrdStatus(FIX::ExecType_CANCELLED));
            resp.setField(FIX::Text("no quote"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          active_orders_[sec->id][clordid] = ord;
        } else if (msgType == "F") {
          resp.getHeader().setField(FIX::MsgType("9"));
          resp.setField(FIX::CxlRejResponseTo(
              FIX::CxlRejResponseTo_ORDER_CANCEL_REQUEST));
          auto symbol = msg.getField(FIX::FIELD::Symbol);
          auto exchange = msg.getField(FIX::FIELD::ExDestination);
          auto sec =
              opentrade::SecurityManager::Instance().Get(exchange, symbol);
          if (!sec) {
            resp.setField(FIX::Text("unknown security"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          auto& actives = active_orders_[sec->id];
          auto clordid = msg.getField(FIX::FIELD::ClOrdID);
          if (used_ids_.find(clordid) != used_ids_.end()) {
            resp.setField(FIX::Text("duplicate ClOrdID"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          used_ids_.insert(clordid);
          auto orig = msg.getField(FIX::FIELD::OrigClOrdID);
          auto it = actives.find(orig);
          if (it == actives.end()) {
            resp.setField(FIX::Text("inactive"));
            resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
            session_->send(resp);
            return;
          }
          resp = msg;
          resp.getHeader().setField(FIX::MsgType("8"));
          resp.setField(FIX::ExecType(FIX::ExecType_CANCELLED));
          resp.setField(FIX::OrdStatus(FIX::ExecType_CANCELLED));
          resp.setField(FIX::TransactTime(FIX::UTCTIMESTAMP()));
          session_->send(resp);
          actives.erase(it);
        }
      },
      boost::posix_time::microseconds(latency_));
}

void SimServer::StartFix(const opentrade::Adapter& adapter) {
  latency_ = atoi(adapter.config("latency").c_str());
  LOG_INFO(adapter.name() << ": latency=" << latency_ << "us");

  auto config_file = adapter.config("config_file");
  if (config_file.empty())
    LOG_FATAL(adapter.name() << ": config_file not given");
  if (!std::ifstream(config_file.c_str()).good())
    LOG_FATAL(adapter.name() << ": Faield to open: " << config_file);

  fix_settings_.reset(new FIX::SessionSettings(config_file));
  fix_store_factory_.reset(new FIX::NullStoreFactory);
  auto file_log_path = fix_settings_->get().getString("FileLogPath");
  if (file_log_path.find("/dev/null") == 0)
    fix_log_factory_.reset(new FIX::NullLogFactory);
  else
    fix_log_factory_.reset(new FIX::AsyncFileLogFactory(*fix_settings_));
  threaded_socket_acceptor_.reset(new FIX::ThreadedSocketAcceptor(
      *this, *fix_store_factory_, *fix_settings_, *fix_log_factory_));
  try {
    threaded_socket_acceptor_->start();
  } catch (FIX::RuntimeError& err) {
    LOG_ERROR("Failed to start simulator: " << err.what());
    return;
  }
}
