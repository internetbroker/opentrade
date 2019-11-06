#ifndef OPENTRADE_CONSOLIDATION_H_
#define OPENTRADE_CONSOLIDATION_H_

#include <tbb/tbb_allocator.h>
#include <set>

#include "indicator_handler.h"
#include "market_data.h"

namespace opentrade {

static const Indicator::IdType kConsolidation = 1;
static const DataSrc kConsolidationSrc("CONS");
static const char* kConsolidationBook = "ConsolidationBook";

template <typename T, template <typename> typename Cmp,
          template <typename> typename Alloc = std::allocator>
struct PriceLevels : public std::set<T, Cmp<T>, Alloc<T>> {
  static inline Cmp<double> kPriceCmp;
  static constexpr bool IsAsk() { return kPriceCmp(0, 1); }
};

struct PriceLevel {
  explicit PriceLevel(double price) : price(price) {}
  double price;
  struct Quote {
    Quote(const Instrument* inst, PriceLevel* parent)
        : inst(inst), parent(parent) {}
    const Instrument* inst;
    PriceLevel* parent;
  };
  typedef std::list<Quote, tbb::tbb_allocator<Quote>> Quotes;
  Quotes quotes;
  std::set<PriceLevel>::iterator
      self;  // for erase myself from levels efficiently
  bool operator<(const PriceLevel& rhs) const { return price < rhs.price; }
  bool operator>(const PriceLevel& rhs) const { return price > rhs.price; }
  auto Insert(const Instrument* inst) {
    quotes.emplace_front(inst, this);
    return quotes.begin();
  }
};

typedef PriceLevels<PriceLevel, std::less, tbb::tbb_allocator> AskLevels;
typedef PriceLevels<PriceLevel, std::greater, tbb::tbb_allocator> BidLevels;
static_assert(AskLevels::IsAsk(), "AskLevels cmp function wrong");
static_assert(!BidLevels::IsAsk(), "BidLevels cmp function wrong");

struct ConsolidationBook : public Indicator {
  static const Indicator::IdType kId = kConsolidation;
  typedef std::unique_lock<std::mutex> Lock;
  std::vector<PriceLevel::Quotes::iterator> ask_quotes;
  std::vector<PriceLevel::Quotes::iterator> bid_quotes;
  AskLevels asks;
  BidLevels bids;
  mutable std::mutex m;
  void Reset();
  template <typename A, typename B>
  void Update(double price, const Instrument* inst, A* a, B* b);
  template <bool reset, typename A>
  void Erase(PriceLevel::Quotes::const_iterator it, A* a);
  template <typename A, typename B>
  void Insert(double price, const Instrument* inst, A* a, B* b);
};

struct ConsolidationHandler : public IndicatorHandler {
  typedef ConsolidationBook Ind;
  ConsolidationHandler() { set_name(kConsolidationBook); }
  Indicator::IdType id() const override { return Ind::kId; }
  void Start() noexcept override;
  void Subscribe(Instrument* inst, bool listen) noexcept override;
  void OnMarketQuote(const Instrument& inst, const MarketData& md,
                     const MarketData& md0) noexcept override;
};

class DummyFeed : public MarketDataAdapter {
 public:
  explicit DummyFeed(const std::string& src) {
    connected_ = 1;
    set_name(src);
    config_["src"] = src;
  }
  void Start() noexcept override {}
  void Stop() noexcept override {}
  void SubscribeSync(const opentrade::Security& sec) noexcept override {}
};

}  // namespace opentrade

#endif  // OPENTRADE_CONSOLIDATION_H_
