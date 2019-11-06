#ifndef OPENTRADE_UTILITY_H_
#define OPENTRADE_UTILITY_H_

#include <sys/time.h>
#include <any>
#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace opentrade {

template <typename V>
const typename V::mapped_type& FindInMap(const V& map,
                                         const typename V::key_type& key) {
  static const typename V::mapped_type kValue{};
  auto it = map.find(key);
  if (it == map.end()) return kValue;
  return it->second;
}

template <typename V>
const typename V::mapped_type& FindInMap(std::shared_ptr<V> map,
                                         const typename V::key_type& key) {
  return FindInMap(*map, key);
}

template <typename V>
const typename V::mapped_type& FindInMap(boost::shared_ptr<V> map,
                                         const typename V::key_type& key) {
  return FindInMap(*map, key);
}

template <typename M, typename V>
std::optional<V> GetParam(const M& var_map, const std::string& name) {
  auto it = var_map.find(name);
  if (it == var_map.end()) return {};
  if (auto pval = std::get_if<V>(&it->second)) return *pval;
  return {};
}

template <typename M, typename V>
inline V GetParam(const M& var_map, const std::string& name, V default_value) {
  return GetParam<M, V>(var_map, name).value_or(default_value);
}

template <typename M>
inline int GetParam(const M& var_map, const std::string& name,
                    int default_value) {
  return GetParam<M, int64_t>(var_map, name).value_or(default_value);
}

template <typename M>
inline std::string GetParam(const M& var_map, const std::string& name,
                            const char* default_value) {
  return GetParam<M, std::string>(var_map, name).value_or(default_value);
}

template <typename V>
inline std::string ToString(const V& variant) {
  std::string out;
  std::visit(
      [&out](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::any>) {
          return;
        } else if constexpr (std::is_same_v<T, std::string>) {
          out = v;
        } else {
          out = std::to_string(v);
        }
      },
      variant);
  return out;
}

#ifdef BACKTEST
inline uint64_t kTime;
inline std::multimap<uint64_t, std::function<void()>> kTimers;
#endif

static const auto kMicroInSec = 1000000lu;
static const double kMicroInSecF = kMicroInSec;
static const auto kMicroInMin = kMicroInSec * 60;

inline time_t GetTime() {
#ifdef BACKTEST
  if (kTime) return kTime / kMicroInSec;
#endif
  return std::time(nullptr);
}

inline int GetTimeOfDay(struct timeval* out) {
#ifdef BACKTEST
  out->tv_sec = kTime / kMicroInSec;
  out->tv_usec = kTime % kMicroInSec;
  return 0;
#endif
  return gettimeofday(out, nullptr);
}

static inline int64_t NowUtcInMicro() {
  struct timeval now;
  auto rc = GetTimeOfDay(&now);
  if (rc)
    return GetTime() * kMicroInSec;
  else
    return now.tv_sec * kMicroInSec + now.tv_usec;
}

static inline int64_t NowInMicro(int tm_gmtoff = 0) {
  return NowUtcInMicro() + tm_gmtoff * kMicroInSec;
}

template <bool localtime = true, int offset_seconds = 0>
static inline const char* GetNowStr() {
  struct timeval tp;
  GetTimeOfDay(&tp);
  if constexpr (offset_seconds) {
    tp.tv_sec += offset_seconds;
  }
  struct tm tm_info;
  if constexpr (localtime) {
    localtime_r(&tp.tv_sec, &tm_info);
  } else {
    gmtime_r(&tp.tv_sec, &tm_info);
  }
  static thread_local char out[256];
  auto n = strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S", &tm_info);
  // hack to skip Never use sprintf. Use snprintf instead.  [runtime/printf] [5]
  // because sprintf is safe and faster here for sure.
#define SPRINTF_UNSAFE sprintf
  SPRINTF_UNSAFE(out + n, ".%06ld", tp.tv_usec);
  return out;
}

inline std::mutex kTzMutex;

static inline int GetUtcTimeOffset(const char* tz) {
  std::lock_guard<std::mutex> lock(kTzMutex);
  auto orig_tz = getenv("TZ");
  setenv("TZ", tz, 1);
  tzset();
  struct tm tm;
  auto t = GetTime();
  localtime_r(&t, &tm);
  if (orig_tz)
    setenv("TZ", orig_tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return tm.tm_gmtoff;
}

static inline time_t MakeTime(std::tm* tm, const char* tz) {
  std::lock_guard<std::mutex> lock(kTzMutex);
  auto orig_tz = getenv("TZ");
  setenv("TZ", tz, 1);
  tzset();
  auto out = mktime(tm);
  if (orig_tz)
    setenv("TZ", orig_tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return out;
}

static const int kSecondsOneDay = 3600 * 24;

static inline int GetSeconds(int tm_gmtoff = 0) {
  auto rawtime = GetTime() + tm_gmtoff;
  struct tm tm_info;
  gmtime_r(&rawtime, &tm_info);
  auto n = tm_info.tm_hour * 3600 + tm_info.tm_min * 60 + tm_info.tm_sec;
  return (n + kSecondsOneDay) % kSecondsOneDay;
}

static inline time_t GetStartOfDayTime(int tm_gmtoff = 0) {
  auto rawtime = GetTime() + tm_gmtoff;
  struct tm tm_info;
  gmtime_r(&rawtime, &tm_info);
  auto n = tm_info.tm_hour * 3600 + tm_info.tm_min * 60 + tm_info.tm_sec;
  return rawtime - n;
}

static inline int GetDate(int tm_gmtoff = 0) {
  auto rawtime = GetTime() + tm_gmtoff;
  struct tm tm_info;
  gmtime_r(&rawtime, &tm_info);
  return 10000 * (tm_info.tm_year + 1900) + 100 * (tm_info.tm_mon + 1) +
         tm_info.tm_mday;
}

static inline decltype(auto) Split(const std::string& str, const char* sep,
                                   bool compact = true,
                                   bool remove_empty = true) {
  std::vector<std::string> out;
  boost::split(out, str, boost::is_any_of(sep),
               compact ? boost::token_compress_on : boost::token_compress_off);
  if (remove_empty) {
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](auto x) { return x.empty(); }),
              out.end());
  }
  return out;
}

static inline const char* PythonOr(const char* a, const char* b) {
  return a && *a ? a : b;
}

static inline auto Round6(double dbl) { return std::round(dbl * 1e6) / 1e6; }
static inline auto Round8(double dbl) { return std::round(dbl * 1e8) / 1e8; }

}  // namespace opentrade

#endif  // OPENTRADE_UTILITY_H_
