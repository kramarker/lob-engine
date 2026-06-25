# lob-engine

A limit order book matching engine in C++ — ingests buy/sell orders, matches them
against a price-time-priority book, and emits fills. The matching core, not a UI or
a trading strategy. Designed for throughput and latency work; baseline benchmarks
are in place, with optimization and latency percentiles to come.

## Problem

What does it take to *match* orders correctly and fast, the way an exchange or a
market maker's internal book does it? This repo builds that core correctness-first,
then will measure and optimize it.

## What's implemented now

A correctness-first, single-threaded matching core with unit-test coverage
(26 tests, all passing):

- **Integer-tick pricing** — prices are `std::int64_t` ticks, never floating point.
- **Limit orders** — `submit` crosses against the opposite side and rests any
  remainder.
- **Buy- and sell-side matching** with price improvement going to the aggressing taker
  (execution happens at the resting maker's price).
- **Partial fills** — both an incoming order partly filling a resting order, and an
  incoming order sweeping multiple resting orders and resting its leftover.
- **Price-time priority** — best price first (across levels) and FIFO within a price
  level, with the within-level invariant checked by assertion on insertion.
- **Cancellation** — `cancel(id)` removes a resting order, backed by an
  `id -> location` index. In this map-of-deques layout the lookup is O(1) but the
  removal within a level is a linear scan; the flat implementation (later) makes it
  O(1).
- **Order types and time-in-force** — market orders (cross at any price, never rest),
  and limit orders with GTC (rest remainder), IOC (discard remainder), or FOK
  (all-or-nothing, rejected atomically if not fully fillable).

Not yet implemented (deliberately, in later stages): the cache-optimized flat-array
book, an arena/object-pool allocator, concurrency, and stop/iceberg order types.
(Baseline benchmarks exist — see Results below.)

## Architecture

- Core book as two sorted maps (bids descending, asks ascending), each price level a
  FIFO `std::deque` of resting orders. **This baseline `std::map` version is what
  exists today.** A custom flat-array / intrusive-linked-list version comes later,
  once profiling shows why the map version is slow (cache misses from pointer
  chasing — that's the lesson).
- Single-threaded core first (correctness); a lock-free SPSC queue on the ingestion
  path comes later.

## Known hard problems to document

- Fixed-point (integer tick) price representation vs. `double` comparison bugs. *(done)*
- Object pool / arena allocator vs. naive `new Order()` per order. *(later)*
- Correct nanosecond-scale benchmarking (warm-up, avoiding dead-code elision, percentiles). *(baseline throughput done; percentiles later)*

## Stack

C++20 · CMake · GoogleTest · Google Benchmark · (later: `perf` · ASan/UBSan)

## Build & test

GoogleTest is fetched automatically via CMake `FetchContent`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Toolchain: CMake ≥ 3.20 · a C++20 compiler · `clang-format` for style. Build with
`-DLOB_ENABLE_SANITIZERS=ON` for ASan/UBSan during development.

## Benchmarks

Microbenchmarks live under `benchmarks/` and use Google Benchmark (fetched via
CMake `FetchContent`). They must be built in **Release** (optimized, `NDEBUG`, so
the price-time-priority assertion is compiled out) to be meaningful:

```bash
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target lob_benchmarks -j
./cmake-build-release/lob_benchmarks                          # all cases
./cmake-build-release/lob_benchmarks --benchmark_filter=BM_MixedWorkload
```

Each case builds a fresh book per iteration (setup excluded from timing via
`PauseTiming`) and reports throughput in orders processed per second.

## Results — baseline (`std::map` book)

These are **baseline** numbers for the correctness-first `std::map`-of-levels
implementation, recorded to anchor later optimization work — *not* a tuned result.
Single-threaded, g++ 16.1.0, `-O3 -DNDEBUG`, batch sizes of 1,000 and 10,000
orders. Throughput in millions of orders/sec; expect a few % run-to-run variance.

| Benchmark | Path exercised | 1k orders | 10k orders |
|-----------|----------------|-----------|------------|
| `BM_RestingInsert`     | rest a non-crossing order            | 29.8 M/s | 73.1 M/s |
| `BM_CrossingFullMatch` | cross, one fill, pop maker           | 34.3 M/s | 35.4 M/s |
| `BM_PartialFills`      | partial fill, maker decremented in place | 33.9 M/s | 34.3 M/s |
| `BM_MixedWorkload`     | random side/price/size over a multi-level book | 23.8 M/s | 18.3 M/s |

Latency percentiles (p50/p99) and a before/after against the cache-optimized book
are deliberately deferred to the optimization stage — the point of this baseline is
to have an honest starting line to beat.
