#include "risk.h"

#include "position.h"
#include "stop_book.h"

namespace opentrade {

std::string Limits::GetString() {
  std::stringstream out;
  out << std::setprecision(15);
  out << "msg_rate=" << msg_rate << '\n'
      << "msg_rate_per_security=" << msg_rate_per_security << '\n'
      << "order_qty=" << order_qty << '\n'
      << "order_value=" << order_value << '\n'
      << "value=" << value << '\n'
      << "turnover=" << turnover << '\n'
      << "total_value=" << total_value << '\n'
      << "total_turnover=" << total_turnover << '\n'
      << "total_long_value=" << total_long_value << '\n'
      << "total_short_value=" << total_short_value;
  return out.str();
}

std::string Limits::FromString(const std::string& str) {
  Limits l;
  for (auto& str : Split(str, ",;\n")) {
    char name[str.size()];
    double value;
    if (sscanf(str.c_str(), "%[^=]=%lf", name, &value) != 2) {
      return "Invalid limits format, expect <name>=<value>[,;<new line>]...";
    }
    if (!strcasecmp(name, "msg_rate"))
      l.msg_rate = value;
    else if (!strcasecmp(name, "msg_rate_per_security"))
      l.msg_rate_per_security = value;
    else if (!strcasecmp(name, "order_qty"))
      l.order_qty = value;
    else if (!strcasecmp(name, "order_value"))
      l.order_value = value;
    else if (!strcasecmp(name, "value"))
      l.value = value;
    else if (!strcasecmp(name, "turnover"))
      l.turnover = value;
    else if (!strcasecmp(name, "total_value"))
      l.total_value = value;
    else if (!strcasecmp(name, "total_turnover"))
      l.total_turnover = value;
    else if (!strcasecmp(name, "total_long_value"))
      l.total_long_value = value;
    else if (!strcasecmp(name, "total_short_value"))
      l.total_short_value = value;
  }
  *this = l;
  return {};
}

static bool CheckMsgRate(const char* name, const AccountBase& acc,
                         Security::IdType sid) {
  auto tm = GetTime();
  auto& l = acc.limits;
  if (l.msg_rate_per_security > 0) {
    auto v = FindInMap(acc.throttle_per_security_in_sec, sid)(tm);
    if (v >= l.msg_rate_per_security) {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "%s limit breach: message rate per second %d > %f", name, v,
               l.msg_rate_per_security);
      kRiskError = buf;
      return false;
    }
  }
  if (l.msg_rate > 0 && acc.throttle_in_sec(tm) >= l.msg_rate) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s limit breach: message rate %d > %f", name,
             acc.throttle_in_sec(tm), l.msg_rate);
    kRiskError = buf;
    return false;
  }
  return true;
}

static bool Check(const char* name, const Order& ord, const AccountBase& acc,
                  const Position* pos) {
  if (!acc.CheckDisabled(name, &kRiskError)) return false;

  if (!CheckMsgRate(name, acc, ord.sec->id)) return false;

  auto& l = acc.limits;

  char buf[256];
  if (l.order_qty > 0 && ord.qty > l.order_qty) {
    snprintf(buf, sizeof(buf), "%s limit breach: single order quantity %f > %f",
             name, ord.qty, l.order_qty);
    kRiskError = buf;
    return false;
  }

  auto m = ord.sec->multiplier * ord.sec->rate;
  auto v = ord.qty * ord.price * m;
  if (l.order_value > 0) {
    if (v > l.order_value) {
      snprintf(buf, sizeof(buf),
               "%s limit breach: single order value %f > %f, multiplier=%f, "
               "currency rate=%f",
               name, v, l.order_value, ord.sec->multiplier, ord.sec->rate);
      kRiskError = buf;
      return false;
    }
  }

  if (!pos) return true;

  if (l.value > 0) {
    double v2;
    auto net = pos->total_bought - pos->total_sold;
    if (ord.IsBuy())
      v2 = std::max(std::abs(net + pos->total_outstanding_buy + v),
                    std::abs(net - pos->total_outstanding_sell));
    else
      v2 = std::max(std::abs(net + pos->total_outstanding_buy),
                    std::abs(net - pos->total_outstanding_sell - v));
    if (v2 > l.value) {
      snprintf(buf, sizeof(buf),
               "%s limit breach: security intraday trade value %f > %f, "
               "multiplier=%f, "
               "currency rate=%f",
               name, v2, l.value, ord.sec->multiplier, ord.sec->rate);
      kRiskError = buf;
      return false;
    }
  }

  if (l.turnover > 0) {
    double v2 = pos->total_bought + pos->total_outstanding_buy +
                pos->total_sold + pos->total_outstanding_sell + v;
    if (v2 > l.turnover) {
      snprintf(buf, sizeof(buf),
               "%s limit breach: security intraday turnover %f > %f, "
               "multiplier=%f, "
               "currency rate=%f",
               name, v2, l.turnover, ord.sec->multiplier, ord.sec->rate);
      kRiskError = buf;
      return false;
    }
  }

  if (l.total_value > 0) {
    double v2;
    auto& pos = acc.position_value;
    auto net = pos.total_bought - pos.total_sold;
    if (ord.IsBuy())
      v2 = std::max(std::abs(net + pos.total_outstanding_buy + v),
                    std::abs(net - pos.total_outstanding_sell));
    else
      v2 = std::max(std::abs(net + pos.total_outstanding_buy),
                    std::abs(net - pos.total_outstanding_sell - v));
    if (v2 > l.total_value) {
      snprintf(buf, sizeof(buf),
               "%s limit breach: total intraday trade value %f > %f", name, v2,
               l.total_value);
      kRiskError = buf;
      return false;
    }
  }

  if (l.total_turnover > 0) {
    auto& pos = acc.position_value;
    double v2 = pos.total_bought + pos.total_outstanding_buy + pos.total_sold +
                pos.total_outstanding_sell + v;
    if (v2 > l.total_turnover) {
      snprintf(buf, sizeof(buf),
               "%s limit breach: total intraday turnover %f > %f", name, v2,
               l.total_turnover);
      kRiskError = buf;
      return false;
    }
  }

  if (l.total_long_value > 0 && ord.IsBuy()) {
    auto v2 = acc.position_value.long_value;
    auto net =
        pos->qty + pos->total_outstanding_buy - pos->total_outstanding_sell;
    auto d = ord.qty;
    if (net < 0) {
      auto tmp = net + ord.qty;
      if (tmp > 0)
        d = tmp;
      else
        d = 0;
    }
    if (d > 0) v2 += d * ord.price * m;
    if (d > 0 && v2 > l.total_long_value) {
      snprintf(buf, sizeof(buf), "%s limit breach: total long value %f > %f",
               name, v2, l.total_long_value);
      kRiskError = buf;
      return false;
    }
  }

  if (l.total_short_value > 0 && !ord.IsBuy()) {
    auto v2 = acc.position_value.short_value;
    auto net =
        pos->qty + pos->total_outstanding_buy - pos->total_outstanding_sell;
    auto d = ord.qty;
    if (net > 0) {
      auto tmp = net - ord.qty;
      if (tmp < 0)
        d = -tmp;
      else
        d = 0;
    }
    if (d > 0) v2 += d * ord.price * m;
    if (d > 0 && v2 > l.total_short_value) {
      snprintf(buf, sizeof(buf), "%s limit breach: total short value %f > %f",
               name, v2, l.total_short_value);
      kRiskError = buf;
      return false;
    }
  }

  return true;
}

bool RiskManager::CheckMsgRate(const Order& ord) {
  if (disabled_) return true;

  assert(ord.sub_account);
  assert(ord.sec);
  assert(ord.user);
  assert(ord.broker_account);

  if (!opentrade::CheckMsgRate("sub_account", *ord.sub_account, ord.sec->id))
    return false;

  if (!opentrade::CheckMsgRate("broker_account", *ord.broker_account,
                               ord.sec->id))
    return false;

  if (!opentrade::CheckMsgRate("user", *ord.user, ord.sec->id)) return false;

  return true;
}

bool RiskManager::Check(const Order& ord) {
  if (disabled_) return true;

  assert(ord.sub_account);
  assert(ord.sec);
  assert(ord.user);
  assert(ord.broker_account);

  if (!StopBookManager::Instance().CheckStop(*ord.sec, ord.sub_account,
                                             &kRiskError))
    return false;

  if (!opentrade::Check(
          "sub_account", ord, *ord.sub_account,
          &PositionManager::Instance().Get(*ord.sub_account, *ord.sec)))
    return false;

  if (!opentrade::Check(
          "broker_account", ord, *ord.broker_account,
          &PositionManager::Instance().Get(*ord.broker_account, *ord.sec)))
    return false;

  if (!opentrade::Check("user", ord, *ord.user,
                        &PositionManager::Instance().Get(*ord.user, *ord.sec)))
    return false;

  if (!ord.destination.empty()) {
    auto acc = AccountManager::Instance().GetBrokerAccount(ord.destination);
    if (acc && !opentrade::Check("destination", ord, *acc, nullptr))
      return false;
  }

  return true;
}

}  // namespace opentrade
