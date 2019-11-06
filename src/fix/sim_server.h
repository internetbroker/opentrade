#ifndef FIX_SIM_SERVER_H_
#define FIX_SIM_SERVER_H_

#include <tbb/concurrent_unordered_set.h>
#include <boost/unordered_map.hpp>
#include <ctime>
#include <fstream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#define throw(...)
#include <quickfix/NullStore.h>
#include <quickfix/Session.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#undef throw

#include "filelog.h"
#include "opentrade/adapter.h"
#include "opentrade/logger.h"
#include "opentrade/security.h"

using Security = opentrade::Security;

class SimServer : public FIX::Application {
 public:
  void onCreate(const FIX::SessionID& session_id) override {
    if (!session_) session_ = FIX::Session::lookupSession(session_id);
  }
  void fromApp(const FIX::Message& msg,
               const FIX::SessionID& session_id) override;
  void onLogon(const FIX::SessionID& session_id) override {}
  void onLogout(const FIX::SessionID& session_id) override {}
  void toApp(FIX::Message& msg, const FIX::SessionID& session_id) override {}
  void toAdmin(FIX::Message& msg, const FIX::SessionID& id) override {}
  void fromAdmin(const FIX::Message&, const FIX::SessionID&) override {}
  void HandleTick(Security::IdType sec, char type, double px, double qty);
  void StartFix(const opentrade::Adapter& adapter);

 protected:
  std::unique_ptr<FIX::SessionSettings> fix_settings_;
  std::unique_ptr<FIX::MessageStoreFactory> fix_store_factory_;
  std::unique_ptr<FIX::LogFactory> fix_log_factory_;
  std::unique_ptr<FIX::ThreadedSocketAcceptor> threaded_socket_acceptor_;
  FIX::Session* session_ = nullptr;
  struct OrderTuple {
    double px = 0;
    double leaves = 0;
    bool is_buy = false;
    FIX::Message resp;
  };
  std::unordered_map<Security::IdType,
                     std::unordered_map<std::string, OrderTuple>>
      active_orders_;
  tbb::concurrent_unordered_set<std::string> used_ids_;
  opentrade::TaskPool tp_;
  int latency_ = 0;  // in microseconds
};

#endif  // FIX_SIM_SERVER_H_
