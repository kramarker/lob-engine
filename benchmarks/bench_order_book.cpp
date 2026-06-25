#include <benchmark/benchmark.h>

#include <cstdint>

#include "lob/order_book.hpp"

namespace {

using lob::Order;
using lob::OrderBook;
using lob::OrderId;
using lob::Price;
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

}  // namespace
