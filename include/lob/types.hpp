#pragma once

#include <cstdint>

namespace lob {

// Core trading value types.
//
// Price is an integer number of ticks, never a floating-point value: matching
// requires exact equality and ordering comparisons, and `double` prices produce
// subtle comparison/rounding bugs. Callers convert real prices to ticks at the
// boundary (e.g. $100.25 with a $0.01 tick -> 10025).
using Price = std::int64_t;

// Order/fill sizes. Unsigned: a negative quantity is never meaningful here.
using Quantity = std::uint64_t;

// Stable per-order identifier supplied by the caller.
using OrderId = std::uint64_t;

// Monotonically increasing arrival stamp assigned by the book. Encodes time
// priority: a lower sequence rested earlier.
using Sequence = std::uint64_t;

}  // namespace lob
