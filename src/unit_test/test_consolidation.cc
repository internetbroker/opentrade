#include "3rd/catch.hpp"
#include "3rd/fakeit.hpp"

using fakeit::Mock;

#include "opentrade/consolidation.h"

namespace opentrade {

struct MockConsolidationHandler : public ConsolidationHandler {
  auto CreateInstrument(const Security& sec, DataSrc src,
                        Instrument* parent = nullptr) {
    return Algo::Subscribe(sec, src, false, parent);
  }
};

struct MockAlgoManager : public AlgoManager {
  struct Strand : public AlgoManager::Strand {
    void post(std::function<void()> func) override { func(); }
  };
  MockAlgoManager() {
    threads_.resize(1);
    runners_ = new AlgoRunner(std::this_thread::get_id());
    strands_ = new Strand{};
  }
};

std::string Stringify(const ConsolidationBook& b) {
  std::stringstream str;
  str << b.asks.size();
  for (auto& p : b.asks) {
    str << '|' << p.price;
    for (auto& q : p.quotes) {
      str << q.inst->src().str();
    }
  }
  str << "    ";
  str << b.bids.size();
  for (auto& p : b.bids) {
    str << '|' << p.price;
    for (auto& q : p.quotes) {
      str << q.inst->src().str();
    }
  }
  return str.str();
}

TEST_CASE("ConsolidationHandler", "[ConsolidationHandler]") {
  auto& md_mngr = MarketDataManager::Reset();
  auto& algo_mngr = AlgoManager::Reset<MockAlgoManager>();
  MockConsolidationHandler handler;
  algo_mngr.AddAdapter(&handler);
  handler.Start();

  md_mngr.AddAdapter(new DummyFeed("A"));
  md_mngr.AddAdapter(new DummyFeed("B"));
  md_mngr.AddAdapter(new DummyFeed("C"));
  md_mngr.AddAdapter(new DummyFeed("D"));

  Security sec;
  sec.exchange = new Exchange;
  auto inst_cons = handler.CreateInstrument(sec, kConsolidationSrc);
  auto inst_a = handler.CreateInstrument(sec, DataSrc("A"), inst_cons);
  auto inst_b = handler.CreateInstrument(sec, DataSrc("B"), inst_cons);
  auto inst_c = handler.CreateInstrument(sec, DataSrc("C"), inst_cons);
  auto inst_d = handler.CreateInstrument(sec, DataSrc("D"), inst_cons);
  handler.Subscribe(inst_cons, false);
  auto& book =
      const_cast<ConsolidationBook&>(*inst_cons->Get<ConsolidationBook>());
  MarketData md;
  auto& quote = md.depth[0];
  auto& ask = quote.ask_price;
  auto& bid = quote.bid_price;
  auto& asks = book.asks;
  auto& bids = book.bids;

  SECTION("Single Src") {
    md = MarketData{};
    auto md0 = md;
    book.Reset();
    ask = 1, bid = 0.5;
    handler.OnMarketQuote(*inst_a, md, md0);
    REQUIRE((asks.size() == 1 && asks.begin()->price == ask &&
             bids.size() == 1 && bids.begin()->price == bid));
    md0 = md;
    ask = 0;
    handler.OnMarketQuote(*inst_a, md, md0);
    REQUIRE(
        (asks.size() == 0 && bids.size() == 1 && bids.begin()->price == bid));
    md0 = md;
    ask = 0.5;
    handler.OnMarketQuote(*inst_a, md, md0);
    REQUIRE((asks.size() == 1 && asks.begin()->price == ask &&
             bids.size() == 1 && bids.begin()->price == bid));
    md0 = md;
    ask = 0.4;
    handler.OnMarketQuote(*inst_a, md, md0);
    REQUIRE(
        (asks.size() == 1 && asks.begin()->price == ask && bids.size() == 0));
    md0 = md;
    bid = 0.6;
    handler.OnMarketQuote(*inst_a, md, md0);
    REQUIRE(
        (asks.size() == 0 && bids.size() == 1 && bids.begin()->price == bid));
  }

  SECTION("Multiple Srcs") {
    md = MarketData{};
    auto md0 = md;
    book.Reset();
    ask = 1, bid = 0.5;
    handler.OnMarketQuote(*inst_a, md, md0);
    handler.OnMarketQuote(*inst_b, md, md0);
    handler.OnMarketQuote(*inst_c, md, md0);
    handler.OnMarketQuote(*inst_d, md, md0);
    REQUIRE((Stringify(book) == "1|1DCBA    1|0.5DCBA"));
    auto md0_a = md, md0_b = md, md0_c = md, md0_d = md;
    ask = 1.1, bid = 0.4;
    handler.OnMarketQuote(*inst_a, md, md0_a);
    REQUIRE((Stringify(book) == "2|1DCB|1.1A    2|0.5DCB|0.4A"));
    md0_a = md;
    ask = 0.9, bid = 0.3;
    handler.OnMarketQuote(*inst_b, md, md0_b);
    md0_b = md;
    REQUIRE((Stringify(book) == "3|0.9B|1DC|1.1A    3|0.5DC|0.4A|0.3B"));
    md = md0_c, ask = 0;
    handler.OnMarketQuote(*inst_c, md, md0_c);
    md0_c = md;
    md = md0_d, ask = 0;
    handler.OnMarketQuote(*inst_d, md, md0_d);
    md0_d = md;
    REQUIRE((Stringify(book) == "2|0.9B|1.1A    3|0.5DC|0.4A|0.3B"));
    md = md0_d, ask = 0.5;
    handler.OnMarketQuote(*inst_d, md, md0_d);
    md0_d = md;
    REQUIRE((Stringify(book) == "3|0.5D|0.9B|1.1A    3|0.5DC|0.4A|0.3B"));
    md = md0_d, ask = 0.4;
    handler.OnMarketQuote(*inst_d, md, md0_d);
    md0_d = md;
    REQUIRE((Stringify(book) == "3|0.4D|0.9B|1.1A    2|0.4A|0.3B"));
    md = md0_d, ask = 0.3;
    handler.OnMarketQuote(*inst_d, md, md0_d);
    md0_d = md;
    REQUIRE((Stringify(book) == "3|0.3D|0.9B|1.1A    1|0.3B"));
    md = md0_a, bid = 0.3;
    handler.OnMarketQuote(*inst_a, md, md0_a);
    md0_a = md;
    REQUIRE((Stringify(book) == "3|0.3D|0.9B|1.1A    1|0.3AB"));
    md = md0_a, bid = 1.2;
    handler.OnMarketQuote(*inst_a, md, md0_a);
    md0_a = md;
    REQUIRE((Stringify(book) == "0    2|1.2A|0.3B"));
  }
}

}  // namespace opentrade
