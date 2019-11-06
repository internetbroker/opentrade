#ifndef OPENTRADE_OPENTICK_H_
#define OPENTRADE_OPENTICK_H_

#include "3rd/json.hpp"
#include "3rd/opentick.hpp"
#include "common.h"
#include "security.h"

namespace opentrade {

using json = nlohmann::json;

class OpenTick : public Singleton<OpenTick> {
 public:
  void Initialize(const std::string& url);
  opentick::ResultSet Request(Security::IdType sec, int interval,
                              time_t start_time, time_t end_time,
                              const std::string& tbl,
                              opentick::Callback callback);

 private:
  opentick::Connection::Ptr conn_;
};

}  // namespace opentrade

#endif  // OPENTRADE_OPENTICK_H_
