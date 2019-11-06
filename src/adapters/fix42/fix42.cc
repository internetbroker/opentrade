#include "fix/fix.h"

extern "C" {
opentrade::Adapter* create() { return new opentrade::Fix42{}; }
}
