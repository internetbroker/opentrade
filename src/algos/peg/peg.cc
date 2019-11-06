#include "../twap/twap.h"

namespace opentrade {

struct Peg : public TWAP {
  double GetLeaves() noexcept override {
    return st_.qty - inst_->total_exposure();
  }
};

}  // namespace opentrade

extern "C" {
opentrade::Adapter* create() { return new opentrade::Peg{}; }
}
