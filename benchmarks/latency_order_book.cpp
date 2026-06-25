// Per-operation latency harness for the order book.
//
// Google Benchmark (see bench_order_book.cpp) reports throughput and aggregate
// timing across repetitions, which answers "how many orders per second" but not
// "what does the tail look like". Matching engines are judged on tail latency,
// so this harness times individual operations, collects the full distribution,
// and reports p50/p90/p99/p99.9/max.
//
// Methodology notes (the measurement is as much the point as the result):
//   - The operations here take tens of nanoseconds. std::chrono::steady_clock on
//     this platform ticks at ~100 ns, too coarse to resolve them, so timing uses
//     the x86 TSC (rdtsc/rdtscp) with lfence serialization to stop the CPU from
//     reordering the counter reads around the work. The TSC is calibrated to
//     nanoseconds once at startup against steady_clock; modern x86 TSCs run at a
//     constant rate independent of frequency scaling, which is what makes that
//     calibration valid.
//   - The rdtsc pair has its own cost; it is measured as an empty timed region
//     ("timer overhead") and printed, so operation rows can be read net of it.
//     Rows are reported gross (timer cost included) and not auto-corrected.
//   - A warm-up phase runs before recording so caches, allocator free lists, and
//     branch predictors are in steady state.
//   - do_not_optimize keeps the optimizer from eliding work whose result is
//     otherwise unused.
//
// This harness is x86-64 specific (it uses rdtsc) and built only with GCC/Clang.

#include <x86intrin.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "lob/order_book.hpp"

namespace {

using lob::Order;
using lob::OrderBook;
using lob::OrderId;
using lob::Price;
using lob::Side;

using Clock = std::chrono::steady_clock;

// Force the compiler to treat `value` as observed, defeating dead-code
// elimination of work whose result we otherwise discard. GCC/Clang idiom.
template <class T>
inline void do_not_optimize(const T& value) {
  __asm__ volatile("" : : "r,m"(value) : "memory");
}

// Serialized TSC reads. lfence keeps the counter read from being hoisted past
// or sunk below the timed region; rdtscp on the closing read waits for prior
// instructions to retire. This is the standard Intel cycle-counting pattern.
inline std::uint64_t tsc_begin() {
  _mm_lfence();
  const std::uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
}

inline std::uint64_t tsc_end() {
  unsigned aux = 0;
  const std::uint64_t t = __rdtscp(&aux);
  _mm_lfence();
  return t;
}

// Nanoseconds per TSC cycle, measured against steady_clock over a fixed wall
// interval. Called once; the result converts cycle deltas to nanoseconds.
double calibrate_ns_per_cycle() {
  using namespace std::chrono_literals;
  const auto wall_start = Clock::now();
  const std::uint64_t tsc_start = tsc_begin();
  while (Clock::now() - wall_start < 100ms) {
    // Busy-wait so the TSC and the wall clock cover the same interval.
  }
  const std::uint64_t tsc_stop = tsc_end();
  const auto wall_stop = Clock::now();

  const double ns = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(wall_stop - wall_start).count());
  const double cycles = static_cast<double>(tsc_stop - tsc_start);
  return ns / cycles;
}

struct Distribution {
  std::uint64_t p50{};
  std::uint64_t p90{};
  std::uint64_t p99{};
  std::uint64_t p999{};
  std::uint64_t max{};
  std::size_t count{};
};

// Nearest-rank percentiles over the collected cycle samples, converted to
// nanoseconds. Mutates (sorts) the input, which callers no longer need.
Distribution summarize(std::vector<std::uint64_t>& cycle_samples, double ns_per_cycle) {
  Distribution d;
  d.count = cycle_samples.size();
  if (cycle_samples.empty()) {
    return d;
  }
  std::sort(cycle_samples.begin(), cycle_samples.end());
  const auto ns_at = [&](double q) {
    const auto idx = static_cast<std::size_t>(q * static_cast<double>(d.count - 1));
    return static_cast<std::uint64_t>(static_cast<double>(cycle_samples[idx]) * ns_per_cycle);
  };
  d.p50 = ns_at(0.50);
  d.p90 = ns_at(0.90);
  d.p99 = ns_at(0.99);
  d.p999 = ns_at(0.999);
  d.max = static_cast<std::uint64_t>(static_cast<double>(cycle_samples.back()) * ns_per_cycle);
  return d;
}

void print_header() {
  std::printf("%-22s %8s %8s %8s %8s %10s\n", "scenario", "p50", "p90", "p99", "p99.9", "max");
  std::printf("%-22s %8s %8s %8s %8s %10s\n", "(ns/op)", "----", "----", "----", "-----", "---");
}

void print_row(const char* name, const Distribution& d) {
  std::printf("%-22s %8llu %8llu %8llu %8llu %10llu\n",
              name,
              static_cast<unsigned long long>(d.p50),
              static_cast<unsigned long long>(d.p90),
              static_cast<unsigned long long>(d.p99),
              static_cast<unsigned long long>(d.p999),
              static_cast<unsigned long long>(d.max));
}

// Baseline cost of the timing itself: an empty timed region. Lets the operation
// rows be read net of the rdtsc pair's cost.
Distribution measure_timer_overhead(int samples, double ns_per_cycle) {
  std::vector<std::uint64_t> cycles;
  cycles.reserve(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    const std::uint64_t t0 = tsc_begin();
    const std::uint64_t t1 = tsc_end();
    cycles.push_back(t1 - t0);
  }
  return summarize(cycles, ns_per_cycle);
}

// Latency of resting a non-crossing limit order into a book already holding
// `depth` resting orders spread across 64 price levels. With no opposite side,
// every submit rests: this isolates level lookup plus the FIFO append.
Distribution measure_resting_insert(int depth, int samples, double ns_per_cycle) {
  OrderBook book;
  OrderId id = 0;
  const auto price_for = [](OrderId i) -> Price { return 9000 - static_cast<Price>(i % 64); };
  for (int i = 0; i < depth; ++i) {
    book.submit(Order{id++, Side::Buy, price_for(id), 10});
  }

  std::vector<std::uint64_t> cycles;
  cycles.reserve(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    const Order order{id++, Side::Buy, price_for(id), 10};
    const std::uint64_t t0 = tsc_begin();
    auto fills = book.submit(order);
    const std::uint64_t t1 = tsc_end();
    do_not_optimize(fills);
    cycles.push_back(t1 - t0);
  }
  return summarize(cycles, ns_per_cycle);
}

// Latency of a crossing buy that fully consumes one resting ask. The book is
// pre-filled with `samples` unit asks at a single price so each measured buy
// pops exactly one maker: this isolates cross detection, one fill, and the
// maker removal (pop plus index erase).
Distribution measure_crossing_match(int samples, double ns_per_cycle) {
  OrderBook book;
  OrderId id = 0;
  for (int i = 0; i < samples; ++i) {
    book.submit(Order{id++, Side::Sell, 100, 10});
  }

  std::vector<std::uint64_t> cycles;
  cycles.reserve(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    const Order order{id++, Side::Buy, 100, 10};
    const std::uint64_t t0 = tsc_begin();
    auto fills = book.submit(order);
    const std::uint64_t t1 = tsc_end();
    do_not_optimize(fills);
    cycles.push_back(t1 - t0);
  }
  return summarize(cycles, ns_per_cycle);
}

// Latency of cancelling a resting order. Orders are spread across 1024 price
// levels (a realistic book shape, not a single pathological level) and then
// cancelled in random id order, so the measurement reflects the index lookup
// plus the in-level scan over a level of modest depth.
Distribution measure_cancel(int samples, double ns_per_cycle) {
  OrderBook book;
  std::vector<OrderId> ids;
  ids.reserve(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    const auto id = static_cast<OrderId>(i);
    book.submit(Order{id, Side::Buy, 9000 - static_cast<Price>(i % 1024), 10});
    ids.push_back(id);
  }
  std::mt19937 rng(2024);
  std::shuffle(ids.begin(), ids.end(), rng);

  std::vector<std::uint64_t> cycles;
  cycles.reserve(static_cast<std::size_t>(samples));
  for (const OrderId id : ids) {
    const std::uint64_t t0 = tsc_begin();
    const bool ok = book.cancel(id);
    const std::uint64_t t1 = tsc_end();
    do_not_optimize(ok);
    cycles.push_back(t1 - t0);
  }
  return summarize(cycles, ns_per_cycle);
}

}  // namespace

int main(int argc, char** argv) {
  // Depth/sample counts are modest defaults chosen to run in well under a
  // second; override from the command line for longer, steadier runs.
  const int depth = argc > 1 ? std::atoi(argv[1]) : 100000;
  const int samples = argc > 2 ? std::atoi(argv[2]) : 100000;

  const double ns_per_cycle = calibrate_ns_per_cycle();

  // Warm up the allocator and caches before recording.
  measure_resting_insert(depth, samples / 10, ns_per_cycle);

  std::printf("lob-engine latency (map book), depth=%d samples=%d, %.3f GHz TSC\n",
              depth,
              samples,
              1.0 / ns_per_cycle);
  print_header();
  {
    auto d = measure_timer_overhead(samples, ns_per_cycle);
    print_row("timer overhead", d);
  }
  {
    auto d = measure_resting_insert(depth, samples, ns_per_cycle);
    print_row("resting insert", d);
  }
  {
    auto d = measure_crossing_match(samples, ns_per_cycle);
    print_row("crossing match", d);
  }
  {
    auto d = measure_cancel(samples, ns_per_cycle);
    print_row("cancel", d);
  }
  return 0;
}
