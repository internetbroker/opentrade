#ifndef OPENTRADE_STOP_BOOK_H_
#define OPENTRADE_STOP_BOOK_H_

#include <tbb/concurrent_unordered_map.h>

#include "account.h"
#include "security.h"

namespace opentrade {

class StopBookManager : public Singleton<StopBookManager> {
 public:
  static void Initialize();

  const auto Get(Security::IdType sec, Security::IdType acc) const {
    return FindInMap(stop_book_, std::make_pair(sec, acc));
  }

  void Set(Security::IdType sec, Security::IdType acc, bool value) {
    stop_book_[std::make_pair(sec, acc)] = value;
  }

  const auto& Get() const { return stop_book_; }

  bool CheckStop(const Security& sec, const SubAccount* acc,
                 std::string* err) const;

 private:
  tbb::concurrent_unordered_map<std::pair<Security::IdType, SubAccount::IdType>,
                                bool>
      stop_book_;
};

}  // namespace opentrade

#endif  // OPENTRADE_STOP_BOOK_H_
