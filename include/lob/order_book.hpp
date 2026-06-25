#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
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
//
// Cancellation is supported via an id -> location index. In this map-of-deques
// layout a cancel still costs a linear scan of the target price level (the
// deque is not addressable by id), which is the baseline's main structural
// weakness; the flat implementation will remove it with intrusive links.
class OrderBook {
public:
  // Submits an order. The order crosses the opposite side wherever its price
  // and type allow (honouring price-time priority), producing zero or more
  // fills; any remainder rests only for a GTC limit order. Returns the fills
  // generated, in execution order.
  //
  // Semantics by type / time-in-force:
  //   - Limit + GTC: cross, then rest any remainder.
  //   - Limit + IOC: cross, then discard any remainder.
  //   - Limit + FOK: cross only if the order can be filled in full; otherwise
  //     no fills are produced and the book is left unchanged.
  //   - Market: cross against all reachable levels regardless of price; never
  //     rests. A market FOK is rejected unless the whole size can be filled.
  std::vector<Fill> submit(const Order& order);

  // Cancels a resting order by id. Returns true if an order with that id was
  // resting and has been removed, false otherwise (unknown id, or an id that
  // already fully traded). Idempotent: cancelling an absent id is a no-op.
  bool cancel(OrderId id);

  // Best resting prices, or std::nullopt when that side is empty.
  std::optional<Price> best_bid() const;
  std::optional<Price> best_ask() const;

  // Total resting quantity at a given price on a given side.
  Quantity quantity_at(Side side, Price price) const;

  // Number of orders currently resting in the book (both sides).
  std::size_t resting_order_count() const noexcept;

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
  using BidMap = std::map<Price, Level, std::greater<Price>>;
  using AskMap = std::map<Price, Level, std::less<Price>>;

  // Where a resting order lives, so cancel can find its level in O(1) before the
  // (linear) scan within the level. Maintained in lockstep with the maps: every
  // order in `index_` is resting, and every resting order is in `index_`.
  struct Locator {
    Side side{};
    Price price{};
  };

  // Crosses `order` against the opposite side, appending fills and decrementing
  // `order.quantity` as it goes. Templated on the opposite map so the same code
  // serves both sides; the only asymmetry — the crossing-price test — is folded
  // into `crosses`. Removes fully consumed makers from the book and the index.
  template <class OppositeMap, class Crosses>
  void match(Order& order, OppositeMap& opposite, const Crosses& crosses, std::vector<Fill>& fills);

  // Total quantity `order` could immediately trade against the opposite side,
  // capped at order.quantity. Used to decide a fill-or-kill before mutating any
  // state. Read-only.
  Quantity fillable_quantity(const Order& order) const;

  BidMap bids_;
  AskMap asks_;
  std::unordered_map<OrderId, Locator> index_;

  // Source of arrival stamps for resting orders (time priority).
  Sequence next_seq_{0};
};

}  // namespace lob
