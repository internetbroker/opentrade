#ifndef OPENTRADE_ACCOUNT_H_
#define OPENTRADE_ACCOUNT_H_

#include <tbb/concurrent_unordered_map.h>
#include <string>
#include <unordered_map>

#include "commission.h"
#include "position_value.h"
#include "risk.h"
#include "security.h"
#include "utility.h"

namespace opentrade {

class ExchangeConnectivityAdapter;

struct AccountBase {
  typedef uint16_t IdType;
  IdType id = 0;
  const char* name = "";
  bool is_disabled = false;
  Limits limits;
  Throttle throttle_in_sec;
  tbb::concurrent_unordered_map<Security::IdType, Throttle>
      throttle_per_security_in_sec;
  PositionValue position_value;

  boost::shared_ptr<const std::string> disabled_reason() const {
    return disabled_reason_.load(boost::memory_order_relaxed);
  }
  void set_disabled_reason(boost::shared_ptr<const std::string> v = {}) {
    disabled_reason_.store(v, boost::memory_order_release);
  }
  bool CheckDisabled(const char* name, std::string* err) const;

 private:
  // different from is_disabled which is persistent in database,
  // disabled_reason is not persistent and designed for OpenRisk
  // https://stackoverflow.com/questions/40223599/what-is-the-difference-between-stdshared-ptr-and-stdexperimentalatomic-sha
  boost::atomic_shared_ptr<const std::string> disabled_reason_;
};

struct BrokerAccount : public AccountBase, public ParamsBase {
  std::string SetParams(const std::string& params);
  const char* adapter_name = "";
  ExchangeConnectivityAdapter* adapter = nullptr;
  const CommissionAdapter* commission_adapter = nullptr;
};

struct SubAccount : public AccountBase {
  typedef std::unordered_map<Exchange::IdType, const BrokerAccount*>
      BrokerAccountMap;
  typedef boost::shared_ptr<const BrokerAccountMap> BrokerAccountMapPtr;
  BrokerAccountMapPtr broker_accounts() const {
    return broker_accounts_.load(boost::memory_order_relaxed);
  }
  void set_broker_accounts(BrokerAccountMapPtr accs) {
    assert(accs);
    broker_accounts_.store(accs, boost::memory_order_release);
  }
  const BrokerAccount* GetBrokerAccount(Exchange::IdType id) const {
    assert(id);
    auto tmp = FindInMap(broker_accounts(), id);
    if (!tmp && id) tmp = FindInMap(broker_accounts(), 0);
    return tmp;
  }

 private:
  boost::atomic_shared_ptr<const BrokerAccountMap> broker_accounts_ =
      BrokerAccountMapPtr(new BrokerAccountMap);
};

struct User : public AccountBase {
  const char* password = "";
  bool is_admin = false;
  typedef std::unordered_map<SubAccount::IdType, const SubAccount*>
      SubAccountMap;
  typedef boost::shared_ptr<const SubAccountMap> SubAccountMapPtr;
  const SubAccount* GetSubAccount(SubAccount::IdType id) const {
    return FindInMap(sub_accounts(), id);
  }
  SubAccountMapPtr sub_accounts() const {
    return sub_accounts_.load(boost::memory_order_relaxed);
  }
  void set_sub_accounts(SubAccountMapPtr accs) {
    assert(accs);
    sub_accounts_.store(accs, boost::memory_order_release);
  }

 private:
  boost::atomic_shared_ptr<const SubAccountMap> sub_accounts_ =
      SubAccountMapPtr(new SubAccountMap);
};

inline const User kEmptyUser;

class AccountManager : public Singleton<AccountManager> {
 public:
  static void Initialize();
  const User* GetUser(const std::string& name) const {
    return FindInMap(user_of_name_, name);
  }
  const User* GetUser(User::IdType id) const { return FindInMap(users_, id); }
  const SubAccount* GetSubAccount(SubAccount::IdType id) const {
    return FindInMap(sub_accounts_, id);
  }
  const SubAccount* GetSubAccount(const std::string& name) const {
    return FindInMap(sub_account_of_name_, name);
  }
  const BrokerAccount* GetBrokerAccount(BrokerAccount::IdType id) const {
    return FindInMap(broker_accounts_, id);
  }
  const BrokerAccount* GetBrokerAccount(const std::string& name) const {
    return FindInMap(broker_account_of_name_, name);
  }

 private:
  tbb::concurrent_unordered_map<User::IdType, User*> users_;
  tbb::concurrent_unordered_map<std::string, User*> user_of_name_;
  tbb::concurrent_unordered_map<SubAccount::IdType, SubAccount*> sub_accounts_;
  tbb::concurrent_unordered_map<std::string, SubAccount*> sub_account_of_name_;
  tbb::concurrent_unordered_map<BrokerAccount::IdType, BrokerAccount*>
      broker_accounts_;
  tbb::concurrent_unordered_map<std::string, BrokerAccount*>
      broker_account_of_name_;
  friend class Connection;
  friend class Backtest;
  friend class PositionManager;
};

}  // namespace opentrade

#endif  // OPENTRADE_ACCOUNT_H_
