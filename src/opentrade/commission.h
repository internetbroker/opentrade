#ifndef OPENTRADE_COMMISSION_H_
#define OPENTRADE_COMMISSION_H_

#include "adapter.h"
#include "common.h"

namespace opentrade {

struct Commission {
  struct Fee {
    double per_share = 0;
    double per_value = 0;
  };
  Fee buy;
  Fee sell;
};

struct Confirmation;

struct CommissionAdapter : public Adapter {
  typedef std::unordered_map<int64_t, Commission> Table;  // <exchange_id, ...>
  CommissionAdapter() {}
  explicit CommissionAdapter(Table&& other) : table_(std::move(other)) {}
  void Start() noexcept override {}
  std::string SetTable(const std::string& tbl_str);
  virtual double Compute(const Confirmation& cm) const noexcept;

 private:
  Table table_;
};

struct CommissionManager : public AdapterManager<CommissionAdapter, kCmPrefix>,
                           public Singleton<CommissionManager> {};

}  // namespace opentrade

#endif  // OPENTRADE_COMMISSION_H_
