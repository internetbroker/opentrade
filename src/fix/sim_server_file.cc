#include "sim_server.h"

#include "opentrade/market_data.h"

struct SimServerFile : public opentrade::MarketDataAdapter, public SimServer {
  void Start() noexcept override;
  void Stop() noexcept override {}
  void SubscribeSync(const Security& sec) noexcept override {}
};

void SimServerFile::Start() noexcept {
  auto bbgid_file = config("bbgid_file");
  if (bbgid_file.empty()) {
    LOG_FATAL(name() << ": bbgid_file not given");
  }

  auto ticks_file = config("ticks_file");
  if (ticks_file.empty()) {
    LOG_FATAL(name() << ": ticks_file not given");
  }

  std::unordered_map<std::string, const Security*> sec_map;
  for (auto& pair : opentrade::SecurityManager::Instance().securities()) {
    sec_map[pair.second->bbgid] = pair.second;
  }

  std::string line;
  std::vector<const Security*> secs;
  std::ifstream ifs(bbgid_file.c_str());
  if (!ifs.good()) {
    LOG_FATAL(name() << ": Can not open " << bbgid_file);
  }
  while (std::getline(ifs, line)) {
    auto sec = sec_map[line];
    secs.push_back(sec);
    if (!sec) {
      LOG_ERROR(name() << ": Unknown bbgid " << line);
      continue;
    }
  }

  if (!std::ifstream(ticks_file.c_str()).good()) {
    LOG_FATAL(name() << ": Can not open " << ticks_file);
  }

  StartFix(*this);
  connected_ = 1;

  std::thread thread([=]() {
    while (true) {
      struct tm tm;
      auto t = time(nullptr);
      gmtime_r(&t, &tm);
      auto n = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
      auto t0 = t - n;
      std::ifstream ifs(ticks_file.c_str());
      std::string line;
      LOG_DEBUG(name() << ": Start to play back");
      auto skip = 0l;
      while (std::getline(ifs, line)) {
        if (skip-- > 0) continue;
        uint32_t hms;
        uint32_t i;
        char type;
        double px;
        double qty;
        if (sscanf(line.c_str(), "%u %u %c %lf %lf", &hms, &i, &type, &px,
                   &qty) != 5)
          continue;
        if (i >= secs.size()) continue;
        t = t0 + hms / 10000 * 3600 + hms % 10000 / 100 * 60 + hms % 100;
        auto now = time(nullptr);
        if (t < now - 3) {
          skip = 1000;
          continue;
        }
        if (now < t) {
          LOG_DEBUG(name() << ": " << hms);
          std::this_thread::sleep_for(std::chrono::seconds(t - now));
        }
        auto sec = secs[i];
        if (!sec) continue;
        switch (type) {
          case 'T':
            Update(sec->id, px, qty);
            break;
          case 'A':
            if (*sec->exchange->name == 'U') qty *= 100;
            Update(sec->id, px, qty, false);
            if (!qty && sec->type == opentrade::kForexPair) qty = 1e9;
            break;
          case 'B':
            if (*sec->exchange->name == 'U') qty *= 100;
            Update(sec->id, px, qty, true);
            if (!qty && sec->type == opentrade::kForexPair) qty = 1e9;
            break;
          default:
            continue;
        }
        HandleTick(sec->id, type, px, qty);
      }
      for (auto& pair : *md_) pair.second = opentrade::MarketData{};
    }
  });
  thread.detach();
}

extern "C" {
opentrade::Adapter* create() { return new SimServerFile{}; }
}
