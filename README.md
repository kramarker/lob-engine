# lob-engine

A limit order book matching engine in C++ — ingests buy/sell orders, matches them
against a price-time-priority book, and emits fills. The matching core, not a UI or
a trading strategy. Designed for throughput and latency work; benchmarking comes in
a later stage.

## Problem

What does it take to *match* orders correctly and fast, the way an exchange or a
market maker's internal book does it? This repo builds that core correctness-first,
then will measure and optimize it.

## What's implemented now

A correctness-first, single-threaded matching core with full unit-test coverage
(15 tests, all passing):

- **Integer-tick pricing** — prices are `std::int64_t` ticks, never floating point.
- **Limit orders** — `add_limit_order` crosses against the opposite side and rests
  any remainder.
- **Buy- and sell-side matching** with price improvement going to the aggressing taker
  (execution happens at the resting maker's price).
- **Partial fills** — both an incoming order partly filling a resting order, and an
  incoming order sweeping multiple resting orders and resting its leftover.
- **Price-time priority** — best price first (across levels) and FIFO within a price
  level, with the within-level invariant checked by assertion on insertion.

Not yet implemented (deliberately, in later stages): benchmarks, the cache-optimized
flat-array book, an arena/object-pool allocator, concurrency, and market/stop/iceberg
order types.

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
- Correct nanosecond-scale benchmarking (warm-up, avoiding dead-code elision, percentiles). *(later)*

## Stack

C++20 · CMake · GoogleTest · (later: Google Benchmark · `perf` · ASan/UBSan)

## Build & test

GoogleTest is fetched automatically via CMake `FetchContent`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Toolchain: CMake ≥ 3.20 · a C++20 compiler · `clang-format` for style. Build with
`-DLOB_ENABLE_SANITIZERS=ON` for ASan/UBSan during development.

## Results

Benchmarks not implemented yet — numbers added only after measuring.

| Metric | Baseline (`std::map`) | Optimized (arena) |
|--------|----------------------|-------------------|
| Throughput (orders/sec) | _TBD_ | _TBD_ |
| p50 match latency | _TBD_ | _TBD_ |
| p99 match latency | _TBD_ | _TBD_ |
