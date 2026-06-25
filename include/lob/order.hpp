#pragma once

#include <cstdint>

#include "lob/side.hpp"
#include "lob/types.hpp"

// Order and execution value types.
//
// These are the inputs and outputs of the matching core: an `Order` is what a
// caller submits, a `Fill` is what the book emits when two orders trade. The
// types are deliberately plain aggregates with no behaviour — the book owns all
// the logic, and keeping these trivially copyable matters on the hot path.

namespace lob {

// How an order is priced against the book.
enum class OrderType : std::uint8_t {
  // Trades only at the order's limit price or better; any unfilled remainder may
  // rest in the book (subject to time-in-force).
  Limit,
  // Trades against the best available prices regardless of level, until filled
  // or the opposite side is exhausted. A market order never rests: its `price`
  // field is ignored.
  Market,
};

// What happens to the portion of an order that cannot trade immediately.
enum class TimeInForce : std::uint8_t {
  // Good-til-cancelled: rest any unfilled remainder in the book. The default,
  // and the only time-in-force under which an order ever rests.
  GTC,
  // Immediate-or-cancel: take whatever is immediately available, discard the
  // rest. Nothing rests.
  IOC,
  // Fill-or-kill: trade in full immediately or not at all. If the book cannot
  // fully fill the order, it is rejected and the book is left unchanged.
  FOK,
};

// An order submitted to the book.
//
// This is an aggregate so it can be brace-initialised positionally
// (`Order{id, side, price, qty}`); `type` and `tif` are trailing members with
// defaults so plain limit-order call sites read as GTC limit orders without
// naming either field.
struct Order {
  OrderId id{};
  Side side{};
  Price price{};        // limit price in ticks; ignored for Market orders
  Quantity quantity{};  // requested size
  OrderType type{OrderType::Limit};
  TimeInForce tif{TimeInForce::GTC};
};

// A single execution produced when an incoming (taker) order matches a resting
// (maker) order. The execution price is always the resting order's price, so
// any price improvement accrues to the aggressing taker.
struct Fill {
  OrderId taker_id{};   // the incoming, aggressing order
  OrderId maker_id{};   // the resting order that was matched against
  Price price{};        // execution price = maker's resting price
  Quantity quantity{};  // size traded in this execution
};

}  // namespace lob
