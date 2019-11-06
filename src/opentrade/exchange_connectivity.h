#ifndef OPENTRADE_EXCHANGE_CONNECTIVITY_H_
#define OPENTRADE_EXCHANGE_CONNECTIVITY_H_

#include "adapter.h"
#include "order.h"

namespace opentrade {

struct ExchangeConnectivityAdapter : public virtual NetworkAdapter {
  virtual std::string Place(const Order& ord) noexcept = 0;
  virtual std::string Cancel(const Order& ord) noexcept = 0;
  void HandleNew(Order::IdType id, const std::string& order_id,
                 int64_t transaction_time = 0);
  void HandleSuspended(Order::IdType id, const std::string& order_id,
                       int64_t transaction_time = 0);
  void HandlePendingNew(Order::IdType id, const std::string& text,
                        int64_t transaction_time = 0);
  void HandlePendingCancel(Order::IdType id, Order::IdType orig_id,
                           int64_t transaction_time = 0);
  void HandleFill(Order::IdType id, double qty, double price,
                  const std::string& exec_id, int64_t transaction_time = 0,
                  bool is_partial = false,
                  ExecTransType exec_trans_type = kTransNew,
                  Confirmation::StrMapPtr misc = Confirmation::StrMapPtr{});
  void HandleCanceled(Order::IdType id, Order::IdType orig_id,
                      const std::string& text, int64_t transaction_time = 0);
  void HandleNewRejected(Order::IdType id, const std::string& text,
                         int64_t transaction_time = 0);
  void HandleCancelRejected(Order::IdType id, Order::IdType orig_id,
                            const std::string& text,
                            int64_t transaction_time = 0);
  void HandleOthers(Order::IdType id, OrderStatus exec_type,
                    const std::string& text, int64_t transaction_time = 0);
};

struct ExchangeConnectivityManager
    : public AdapterManager<ExchangeConnectivityAdapter, kEcPrefix>,
      public Singleton<ExchangeConnectivityManager> {
  bool Place(Order* ord);
  bool Cancel(const Order& orig_ord);
  void HandleFilled(Order* ord, double qty, double price,
                    const std::string& exec_id);
  void ClearUnformed(int offset);
};

}  // namespace opentrade

#endif  // OPENTRADE_EXCHANGE_CONNECTIVITY_H_
