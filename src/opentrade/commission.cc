#include "commission.h"

#include "order.h"

namespace opentrade {
double CommissionAdapter::Compute(const Confirmation& cm) const noexcept {
  auto it = table_.find(cm.order->sec->exchange->id);
  if (it == table_.end()) it = table_.find(0);
  if (it == table_.end()) return 0;
  auto& f = cm.order->IsBuy() ? it->second.buy : it->second.sell;
  if (f.per_share > 0) return f.per_share * cm.last_shares;
  if (f.per_value > 0) return f.per_value * cm.last_shares * cm.last_px;
  return 0;
}

std::string CommissionAdapter::SetTable(const std::string& tbl_str) {
  for (auto& str : Split(tbl_str, " \t|")) {
    char name[str.size()];
    double value;
    if (sscanf(str.c_str(), "%[^=]=%lf", name, &value) != 2) {
      return "Invalid commission format, expect "
             "<name>=<value>[<space><tab>|]...";
    }
    bool is_buy = strstr(name, "buy_") == name;
    bool is_sell = is_buy ? false : strstr(name, "sell_") == name;
    auto p = name + (is_buy ? 4 : (is_sell ? 5 : 0));
    bool per_value = strstr(p, "per_value") == p;
    bool per_share = per_value ? false : strstr(p, "per_share") == p;
    p += 9;
    if ((!per_value && !per_share) || (*p && *p != '_')) {
      return "Invalid commission name " + std::string(name) +
             ", expect per_share or per_value or with <side>_ prefix and "
             "_<exchange_name> suffix";
    }
    auto exch_id = 0;
    if (*p) {
      p += 1;
      auto exch = SecurityManager::Instance().GetExchange(p);
      if (!exch) {
        return "Invalid exchange name in commission: \"" + std::string(p) +
               "\"";
      }
      exch_id = exch->id;
    }
    auto& cm = table_[exch_id];
    if (per_value) {
      if (is_buy)
        cm.buy.per_value = value;
      else if (is_sell)
        cm.sell.per_value = value;
      else
        cm.buy.per_value = cm.sell.per_value = value;
    } else if (per_share) {
      if (is_buy)
        cm.buy.per_share = value;
      else if (is_sell)
        cm.sell.per_share = value;
      else
        cm.buy.per_share = cm.sell.per_share = value;
    }
  }
  return {};
}
}  // namespace opentrade
