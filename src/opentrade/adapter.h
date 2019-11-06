#ifndef OPENTRADE_ADAPTER_H_
#define OPENTRADE_ADAPTER_H_

#include <tbb/atomic.h>
#include <boost/lexical_cast.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "utility.h"

namespace opentrade {

static const char* kApiVersion =
#ifdef BACKTEST
    "backtest_"
#endif
    "1.3.3";

class Adapter {
 public:
  virtual ~Adapter();
  typedef std::unordered_map<std::string, std::string> StrMap;
  const std::string& name() const { return name_; }
  void set_name(const std::string& name) { name_ = name; }
  void set_config(const StrMap& config) { config_ = config; }
  std::string GetVersion() const { return kApiVersion; }
  typedef Adapter* (*CFunc)();
  typedef std::function<Adapter*()> Func;
  Adapter* Clone() {
    if (!create_func_) return this;
    auto inst = create_func_();
    inst->set_name(name());
    inst->set_config(config());
    return inst;
  }
  const StrMap& config() const { return config_; }
  template <typename T = std::string>
  T config(const std::string& name, T default_value = {}) const {
    auto str = FindInMap(config_, name);
    if (str.empty()) return default_value;
    if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
      return str;
    } else {
      try {
        return boost::lexical_cast<T>(str);
      } catch (const std::bad_cast&) {
        return default_value;
      }
    }
  }
  static Adapter* Load(const std::string& sofile);
  virtual void Start() noexcept = 0;
  const auto& create_func() { return create_func_; }
  void set_create_func(Func f) {
    assert(!create_func_);
    create_func_ = f;
  }

 protected:
  std::string name_;
  StrMap config_;
  Func create_func_;
};

class NetworkAdapter : public Adapter {
 public:
  virtual void Reconnect() noexcept {}
  virtual void Stop() noexcept = 0;
  virtual bool connected() const noexcept { return 1 == connected_; }

 protected:
  tbb::atomic<int> connected_ = 0;
};

inline const std::string kAdapterPrefixes[] = {"", "ec_", "md_", "cm_"};

enum AdapterPrefix { kEmptyPrefix, kEcPrefix, kMdPrefix, kCmPrefix };

template <typename T, AdapterPrefix prefix = kEmptyPrefix>
class AdapterManager {
 public:
  typedef std::unordered_map<std::string, T*> AdapterMap;
  void AddAdapter(T* adapter) { adapters_[adapter->name()] = adapter; }
  template <typename B>
  void AddAdapter() {
    static_assert(std::is_base_of<T, B>::value);
    auto adapter = new B;
    adapter->set_create_func([]() { return new B; });
    AddAdapter(adapter);
  }
  T* GetAdapter(const std::string& name) {
    auto out = FindInMap(adapters_, name);
    if (out) return out;
    if constexpr (prefix > 0) {
      auto& p = kAdapterPrefixes[prefix];
      if (name.find(p) == 0) return {};
      return FindInMap(adapters_, p + name);
    }
    return {};
  }
  const AdapterMap& adapters() { return adapters_; }

 private:
  AdapterMap adapters_;
};

}  // namespace opentrade

#endif  // OPENTRADE_ADAPTER_H_
