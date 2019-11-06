#ifndef OPENTRADE_COMMON_H_
#define OPENTRADE_COMMON_H_

#include <boost/atomic.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <string>
#include <unordered_map>

#include "task_pool.h"
#include "utility.h"

namespace opentrade {

static inline const std::string kEmptyStr;
namespace fs = boost::filesystem;
static inline const fs::path kAlgoPath = fs::path(".") / "algos";
static inline const fs::path kStorePath = fs::path(".") / "store";

struct ParamsBase {
  typedef std::unordered_map<std::string, std::string> StrMap;
  typedef boost::shared_ptr<const StrMap> StrMapPtr;

  std::string GetParam(const std::string& k) const {
    return FindInMap(params(), k);
  }

  StrMapPtr params() const { return params_.load(boost::memory_order_relaxed); }

  std::string SetParams(const std::string& params) {
    StrMap tmp;
    for (auto& str : Split(params, ",;\n")) {
      char k[str.size()];
      char v[str.size()];
      if (sscanf(str.c_str(), "%[^=]=%s", k, v) != 2) {
        return "Invalid params format, expect <name>=<value>[,;<new line>]...";
      }
      tmp.emplace((const char*)k, (const char*)v);
    }
    params_.store(StrMapPtr(new StrMap(std::move(tmp))),
                  boost::memory_order_release);
    return {};
  }

  std::string GetParamsString() const {
    std::string out;
    for (auto& pair : *params()) {
      if (!out.empty()) out += "\n";
      out += pair.first + "=" + pair.second;
    }
    return out;
  }

 private:
  boost::atomic_shared_ptr<const StrMap> params_ = StrMapPtr(new StrMap);
};

template <typename V>
class Singleton {
 public:
  static V& Instance() { return *kInstance; }

#ifdef UNIT_TEST
  template <typename T = V>
  static V& Reset() {
    delete kInstance;
    kInstance = new T{};
    return *kInstance;
  }
#endif

  Singleton(const Singleton&) = delete;
  Singleton& operator=(const Singleton&) = delete;

 protected:
  Singleton() {}

 private:
  static inline V* kInstance = new V{};
};

inline TaskPool kTimerTaskPool;
inline TaskPool kWriteTaskPool;
inline TaskPool kDatabaseTaskPool;
}  // namespace opentrade

#endif  // OPENTRADE_COMMON_H_
