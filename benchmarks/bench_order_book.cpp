#include <benchmark/benchmark.h>

#include <cstdint>

#include "lob/order_book.hpp"

namespace {

using lob::Order;
using lob::OrderBook;
using lob::OrderId;
using lob::Price;
using lob::Quantity;
using lob::Side;

// Insert K non-crossing resting orders into a fresh book. With only buys present
// and no asks to cross, nothing matches, so this isolates the cost of resting an
// order: finding or creating its price level and appending to that level's queue.
void BM_RestingInsert(benchmark::State& state) {
  const int k = static_cast<int>(state.range(0));
  for (auto _ : state) {
    OrderBook book;
    for (int i = 0; i < k; ++i) {
      const Price price = 1000 - (i % 256);
      book.add_limit_order(Order{static_cast<OrderId>(i), Side::Buy, price, 10});
    }
    benchmark::DoNotOptimize(book);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * k);
}
BENCHMARK(BM_RestingInsert)->Arg(1000)->Arg(10000);

// Set up K resting asks at one price (setup excluded from timing), then time K
// crossing buys that each fully consume one resting order. This isolates the
// full-match path: cross detection, emitting one fill, and popping the maker.
void BM_CrossingFullMatch(benchmark::State& state) {
  const int k = static_cast<int>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook book;
    for (int i = 0; i < k; ++i) {
      book.add_limit_order(Order{static_cast<OrderId>(i), Side::Sell, 100, 10});
    }
    state.ResumeTiming();

    for (int i = 0; i < k; ++i) {
      auto fills = book.add_limit_order(Order{static_cast<OrderId>(k + i), Side::Buy, 100, 10});
      benchmark::DoNotOptimize(fills);
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * k);
}
BENCHMARK(BM_CrossingFullMatch)->Arg(1000)->Arg(10000);

// Rest one large ask (setup excluded), then time K unit-sized crossing buys that
// each only partially fill it. This isolates the partial-fill path: the maker
// stays at the front of its level and is decremented in place every time.
void BM_PartialFills(benchmark::State& state) {
  const int k = static_cast<int>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook book;
    // Sized so K unit buys never fully deplete the resting order.
    book.add_limit_order(Order{0, Side::Sell, 100, static_cast<Quantity>(k) + 1});
    state.ResumeTiming();

    for (int i = 0; i < k; ++i) {
      auto fills = book.add_limit_order(Order{static_cast<OrderId>(i + 1), Side::Buy, 100, 1});
      benchmark::DoNotOptimize(fills);
    }
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * k);
}
BENCHMARK(BM_PartialFills)->Arg(1000)->Arg(10000);

}  // namespace
