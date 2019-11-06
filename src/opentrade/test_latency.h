#ifndef OPENTRADE_TEST_LATENCY_H_
#define OPENTRADE_TEST_LATENCY_H_
#ifdef TEST_LATENCY

#include "algo.h"
#include "exchange_connectivity.h"
#include "market_data.h"

namespace opentrade {

struct TestLatencyMd : public MarketDataAdapter {
  void Start() noexcept override {
    connected_ = 1;
    static TaskPool kTaskPool;
    kTaskPool.AddTask([this] {
      usleep(1e5);
      LOG_INFO(secs_.size() << " securities subscribed");
      static uint32_t kSeed;
      while (true) {
        for (auto sec : secs_) {
          Update(sec->id, 0.01, 100, NowUtcInMicro());
          usleep(1);
          Update(sec->id, 0.01, rand_r(&kSeed), false, 0, NowUtcInMicro());
          usleep(1);
        }
      }
    });
  }
  void Stop() noexcept override {}
  void SubscribeSync(const opentrade::Security& sec) noexcept override {
    secs_.push_back(&sec);
  }

 private:
  std::vector<const opentrade::Security*> secs_;
};

struct TestlatencyAlgo : public Algo {
  TestlatencyAlgo() { name_ = "_test_latency"; }
  std::string OnStart(const ParamMap& params) noexcept {
    set_user(AccountManager::Instance().GetUser("test"));
    acc_ = AccountManager::Instance().GetSubAccount("test");
    for (auto i = 0; i < 10; ++i) {
      auto sec = SecurityManager::Instance().Get(i + 1);
      if (!sec) continue;
      Subscribe(*sec);
    }
    return {};
  }
  void OnMarketTrade(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept {
    Place(inst, md.tm);
  }
  void OnMarketQuote(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept {
    Place(inst, md.tm);
  }

  void Place(const Instrument& inst, time_t tm) {
    Contract c;
    c.sub_account = acc_;
    c.qty = 100;
    c.price = 0.01;
    c.tm_for_test_latency = tm;
    Algo::Place(c, const_cast<Instrument*>(&inst));
  }

 private:
  const SubAccount* acc_;
};

struct TestLatencyEc : public ExchangeConnectivityAdapter {
  void Start() noexcept override {
    connected_ = 1;
    kLatencies.reserve(100000);
  }
  void Stop() noexcept override {}
  std::string Place(const opentrade::Order& ord) noexcept override {
    static time_t kLastSampleTm = NowUtcInMicro();
    auto now = NowUtcInMicro();
    auto latency = now - ord.tm_for_test_latency;
    kLatencies.push_back(latency);
    static int kN;
    if (now - kLastSampleTm >= 1000000) {
      kN += 1;
      static TaskPool kTaskPool;
      kTaskPool.AddTask([latencies = new decltype(kLatencies)(kLatencies)] {
        std::sort(latencies->begin(), latencies->end());
        auto mean = std::accumulate(latencies->begin(), latencies->end(), 0) /
                    latencies->size();
        LOG_INFO("sample size="
                 << latencies->size() << ", min=" << latencies->front()
                 << ", max=" << latencies->back() << ", mean=" << mean
                 << ", median=" << latencies->at(latencies->size() / 2)
                 << ", 90th percentile="
                 << latencies->at(latencies->size() * 9 / 10)
                 << ", 99th percentile="
                 << latencies->at(latencies->size() * 99 / 100) << " (us)");
        if (kN == 10) {
          LOG_FATAL("done");
        }
      });
      kLatencies.clear();
      kLastSampleTm = now;
    }
    return {};
  }
  std::string Cancel(const opentrade::Order& ord) noexcept override {
    return {};
  }
  static inline std::vector<time_t> kLatencies;
};

}  // namespace opentrade

#endif  // TEST_LATENCY
#endif  // OPENTRADE_TEST_LATENCY_H_
