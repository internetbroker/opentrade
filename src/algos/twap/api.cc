#include "twap.h"

extern "C" {
opentrade::Adapter* create() { return new opentrade::TWAP{}; }
}
