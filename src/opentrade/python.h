#ifndef OPENTRADE_PYTHON_H_
#define OPENTRADE_PYTHON_H_

#include <boost/python.hpp>

#include "algo.h"
#include "connection.h"

namespace opentrade {

namespace bp = boost::python;

struct PyModule {
  bp::object on_start;
  bp::object on_modify;
  bp::object on_stop;
  bp::object on_market_trade;
  bp::object on_market_quote;
  bp::object on_indicator;
  bp::object on_confirmation;
  bp::object test;
  bp::object get_param_defs;
};

class Python : public Algo {
 public:
  static Python* LoadModule(const std::string& module_name);
  static Python* Load(const std::string& module_name);
  static Python* LoadTest(const std::string& module_name,
                          const std::string& token);
  std::string OnStart(const ParamMap& params) noexcept override;
  void OnModify(const ParamMap& params) noexcept override;
  std::string Test() noexcept override;
  void OnStop() noexcept override;
  void OnMarketTrade(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override;
  void OnMarketQuote(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override;
  void OnConfirmation(const Confirmation& cm) noexcept override;
  const ParamDefs& GetParamDefs() noexcept override;
  void OnIndicator(Indicator::IdType id,
                   const Instrument& inst) noexcept override;
  void SetTimeout(bp::object func, double seconds);

  Instrument* Subscribe(const Security& sec, DataSrc src, bool listen) {
    return Algo::Subscribe(sec, src, listen);
  }
  void Stop() { Algo::Stop(); }
  Order* Place(const Contract& contract, Instrument* inst) {
    return Algo::Place(contract, inst);
  }
  void Cross(double qty, double price, OrderSide side, const SubAccount* acc,
             Instrument* inst) {
    return Algo::Cross(qty, price, side, acc, inst);
  }

 private:
  PyModule py_;
  ParamDefs def_;
  bp::object obj_;
  std::string test_token_;
};

void InitalizePy();
void PrintPyError(const char*, bool fatal = false);
bp::object GetCallable(const bp::object& m, const char* name);

inline bp::object kOpentrade;

}  // namespace opentrade

#endif  // OPENTRADE_PYTHON_H_
