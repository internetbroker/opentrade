#ifndef OPENTRADE_POSITION_VALUE_H_
#define OPENTRADE_POSITION_VALUE_H_

#include <cassert>

namespace opentrade {

struct PositionValue {
  double long_value = 0;
  double short_value = 0;
  double total_bought = 0;
  double total_sold = 0;
  double total_outstanding_buy = 0;
  double total_outstanding_sell = 0;

  void HandleNew(bool is_buy, double qty, double price, double multiplier);
  void HandleTrade(bool is_buy, double qty, double price, double price0,
                   double multiplier, bool is_bust, bool is_otc);
  void HandleFinish(bool is_buy, double leaves_qty, double price0,
                    double multiplier);
};

inline void PositionValue::HandleNew(bool is_buy, double qty, double price,
                                     double multiplier) {
  assert(qty > 0);
  auto value = qty * price * multiplier;
  if (is_buy) {
    total_outstanding_buy += value;
  } else {
    total_outstanding_sell += value;
  }
}

inline void PositionValue::HandleTrade(bool is_buy, double qty, double price,
                                       double price0, double multiplier,
                                       bool is_bust, bool is_otc) {
  assert(qty > 0);
  if (!is_buy) qty = -qty;
  auto value = qty * price * multiplier;
  if (is_otc) {
    // do nothing
  } else if (!is_bust) {
    auto value0 = qty * price0 * multiplier;
    if (value > 0) {
      total_outstanding_buy -= value0;
      total_bought += value;
    } else {
      total_outstanding_sell -= -value0;
      total_sold += -value;
    }
  } else {
    if (value > 0) {
      total_bought -= value;
    } else {
      total_sold -= -value;
    }
  }
}

inline void PositionValue::HandleFinish(bool is_buy, double leaves_qty,
                                        double price0, double multiplier) {
  assert(leaves_qty);
  auto value = leaves_qty * price0 * multiplier;
  if (is_buy) {
    total_outstanding_buy -= value;
  } else {
    total_outstanding_sell -= value;
  }
}

}  // namespace opentrade

#endif  // OPENTRADE_POSITION_VALUE_H_
