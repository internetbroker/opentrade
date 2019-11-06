#include "3rd/catch.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>

#include "opentrade/utility.h"

static inline std::string FacetNow() {
  static std::locale loc(std::cout.getloc(), new boost::posix_time::time_facet(
                                                 "%Y-%m-%d %H:%M:%S.%f"));

  std::stringstream ss;
  ss.imbue(loc);
  ss << boost::posix_time::microsec_clock::universal_time();
  return ss.str();
}

namespace opentrade {

TEST_CASE("Utility", "[Utility]") {
  std::cout << "time_facet " << FacetNow() << " vs " << FacetNow() << std::endl;
  std::cout << "GetNowStr  " << GetNowStr<false>() << " vs "
            << GetNowStr<false>() << std::endl;
}

}  // namespace opentrade
