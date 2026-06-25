# lob-engine

A limit order book matching engine in C++ — ingests buy/sell orders, matches them
against a price-time-priority book, and emits fills. Built for throughput and
latency, benchmarked, not a UI or a trading strategy.

## Problem

What does it take to *match* orders correctly and fast, the way an exchange or a
market maker's internal book does it? This repo answers that with measured numbers.

## Architecture (planned)

- Core book as two sorted structures (bids desc, asks asc): a baseline
  `std::map<price, std::deque<Order>>` version, then a custom flat-array /
  intrusive-linked-list version once profiling shows why the map version is slow
  (cache misses from pointer chasing — that's the lesson).
- `MatchingEngine` separated from `OrderBook` storage behind an interface, so
  implementations can be swapped and compared.
- Single-threaded core first (correctness), then a lock-free SPSC queue on the
  ingestion path.

## Known hard problems to document

- Fixed-point (integer tick) price representation vs. `double` comparison bugs.
- Object pool / arena allocator vs. naive `new Order()` per order.
- Correct nanosecond-scale benchmarking (warm-up, avoiding dead-code elision, percentiles).

## Stack

C++17/20 · CMake · GoogleTest · Google Benchmark · `perf` · ASan/UBSan

## Build & test (planned)

> Tooling is configured; **no engine source exists yet**, so these commands set up
> and build the (currently empty) project. They become functional as `src/`,
> `tests/`, and `benchmarks/` are implemented.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # configure (succeeds with empty src/)
cmake --build build -j                           # build once sources exist
ctest --test-dir build --output-on-failure       # run GoogleTest suite (once tests exist)
./build/lob_benchmarks                            # run Google Benchmark suite (once benches exist)
```

Toolchain: CMake ≥ 3.20 · a C++20 compiler · `clang-format` / `clang-tidy` for
style and static analysis. GoogleTest and Google Benchmark are fetched
automatically via CMake `FetchContent` when the corresponding sources are added.
Build with `-DLOB_ENABLE_SANITIZERS=ON` for ASan/UBSan during development.

## Results

| Metric | Baseline (`std::map`) | Optimized (arena) |
|--------|----------------------|-------------------|
| Throughput (orders/sec) | _TBD_ | _TBD_ |
| p50 match latency | _TBD_ | _TBD_ |
| p99 match latency | _TBD_ | _TBD_ |

> Numbers added only after measuring.
