#ifndef OPENTRADE_SERVER_H_
#define OPENTRADE_SERVER_H_

#include "algo.h"

namespace opentrade {

class Server {
 public:
  static void Start(int port, int nthreads = 1);
  static void Publish(Confirmation::Ptr cm);
  static void Publish(const Algo& algo, const std::string& status,
                      const std::string& body, uint32_t seq);
  static void Publish(const std::string& msg, const SubAccount* acc = nullptr);
  static void PublishTestMsg(const std::string& token, const std::string& msg,
                             bool stopped = false);
  static void CloseConnection(User::IdType id);
  static void Trigger(const std::string& cmd);
  static void Stop();
};

}  // namespace opentrade

#endif  // OPENTRADE_SERVER_H_
