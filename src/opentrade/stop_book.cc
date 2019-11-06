#include "stop_book.h"

#include "database.h"

namespace opentrade {

void StopBookManager::Initialize() {
  auto& self = Instance();
  auto sql = Database::Session();

  auto query = "select security_id, sub_account_id from stop_book";
  soci::rowset<soci::row> st = sql->prepare << query;
  for (auto it = st.begin(); it != st.end(); ++it) {
    auto i = 0;
    auto sec = Database::GetValue(*it, i++, 0);
    auto acc = Database::GetValue(*it, i++, 0);
    self.stop_book_.emplace(std::make_pair(sec, acc), true);
  }
}

bool StopBookManager::CheckStop(const Security& sec, const SubAccount* acc,
                                std::string* err) const {
  char buf[256];
  if (Get(sec.id, acc ? acc->id : 0)) {
    if (acc)
      snprintf(buf, sizeof(buf),
               "security \"%s\" of sub_account \"%s\" is stopped", sec.symbol,
               acc->name);
    else
      snprintf(buf, sizeof(buf), "security \"%s\" is stopped", sec.symbol);
    *err = buf;
    return false;
  }

  if (acc) {
    if (Get(sec.id, 0)) {
      snprintf(buf, sizeof(buf), "security \"%s\" is stopped", sec.symbol);
      *err = buf;
      return false;
    }
  }
  return true;
}

}  // namespace opentrade
