#pragma once

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "lob/order.hpp"
#include "lob/side.hpp"
#include "lob/types.hpp"

namespace lob {

// A price-time-priority limit order book.
//
// This is the correctness-first baseline implementation: each side is a
// std::map of price levels, and each level is a FIFO queue of resting orders.
// (A cache-optimized rewrite over a flat structure comes later; the public
// interface here is what that rewrite will have to match.)
class OrderBook {
public:
  // Submits a limit order. The order first crosses against the opposite side
  // wherever its price allows (honouring price-time priority), producing zero
  // or more fills; any quantity that remains afterwards rests in the book.
  // Returns the fills generated, in execution order.
  std::vector<Fill> add_limit_order(Order order);

  // Best resting prices, or std::nullopt when that side is empty.
  std::optional<Price> best_bid() const;
  std::optional<Price> best_ask() const;

  // Total resting quantity at a given price on a given side.
  Quantity quantity_at(Side side, Price price) const;

  // True when neither side holds any resting quantity.
  bool empty() const noexcept;

private:
  struct RestingOrder {
    OrderId id{};
    Quantity quantity{};
    Sequence seq{};
  };

  using Level = std::deque<RestingOrder>;

  // Bids are ordered highest-price-first, asks lowest-price-first, so the best
  // price on each side is always begin().
  std::map<Price, Level, std::greater<Price>> bids_;
  std::map<Price, Level, std::less<Price>> asks_;

  // Source of arrival stamps for resting orders (time priority).
  Sequence next_seq_{0};
};

}  // namespace lob
