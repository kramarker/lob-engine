#pragma once

#include "lob/side.hpp"
#include "lob/types.hpp"

namespace lob {

// An incoming limit order submitted to the book.
struct Order {
  OrderId id{};
  Side side{};
  Price price{};        // limit price, in ticks
  Quantity quantity{};  // requested size
};

// A single execution produced when an incoming (taker) order matches a resting
// (maker) order. The execution price is always the resting order's price.
struct Fill {
  OrderId taker_id{};   // the incoming, aggressing order
  OrderId maker_id{};   // the resting order that was matched against
  Price price{};        // execution price = maker's resting price
  Quantity quantity{};  // size traded in this execution
};

}  // namespace lob
