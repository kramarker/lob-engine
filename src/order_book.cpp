#include "lob/order_book.hpp"

#include <cassert>

namespace lob {

template <class OppositeMap, class Crosses>
void OrderBook::match(Order& order, OppositeMap& opposite, const Crosses& crosses,
                      std::vector<Fill>& fills) {
  // Walk the opposite side best-price-first. begin() is the best level by the
  // map's comparator, and within a level the deque front is the oldest resting
  // order, so consuming front-to-back honours price-time priority on both axes.
  while (order.quantity > 0 && !opposite.empty()) {
    const auto best = opposite.begin();
    const Price level_price = best->first;
    if (!crosses(level_price)) {
      break;  // best opposite price is beyond our limit: no longer crossing.
    }
    Level& level = best->second;

    // Consume resting orders this incoming order can fully absorb.
    while (!level.empty() && order.quantity >= level.front().quantity) {
      const RestingOrder& maker = level.front();
      fills.push_back({order.id, maker.id, level_price, maker.quantity});
      order.quantity -= maker.quantity;
      index_.erase(maker.id);
      level.pop_front();
    }

    if (level.empty()) {
      opposite.erase(best);
    } else if (order.quantity > 0) {
      // The incoming order is smaller than the oldest resting order left at this
      // price: fill it fully and decrement the maker in place. The maker keeps
      // its place at the front of the level, so its index entry is untouched.
      RestingOrder& maker = level.front();
      fills.push_back({order.id, maker.id, level_price, order.quantity});
      maker.quantity -= order.quantity;
      order.quantity = 0;
    }
  }
}

std::vector<Fill> OrderBook::add_limit_order(Order order) {
  std::vector<Fill> fills;

  if (is_buy(order.side)) {
    const auto crosses = [&](Price ask_price) { return order.price >= ask_price; };
    match(order, asks_, crosses, fills);
  } else {
    const auto crosses = [&](Price bid_price) { return order.price <= bid_price; };
    match(order, bids_, crosses, fills);
  }

  if (order.quantity > 0) {
    // Price-time priority is maintained on two axes: across price levels by the
    // maps' key ordering (best price is always begin()), and within a level by
    // appending to the back so earlier arrivals — carrying a lower sequence —
    // stay at the front where matching consumes them first. The assertion pins
    // that within-level invariant.
    const RestingOrder resting{order.id, order.quantity, next_seq_++};
    Level& level = is_buy(order.side) ? bids_[order.price] : asks_[order.price];
    assert((level.empty() || level.back().seq < resting.seq) &&
           "price-time priority: a later arrival must rest behind earlier ones");
    level.push_back(resting);
    index_.emplace(order.id, Locator{order.side, order.price});
  }

  return fills;
}

bool OrderBook::cancel(OrderId id) {
  const auto it = index_.find(id);
  if (it == index_.end()) {
    return false;  // unknown id, or one that already fully traded.
  }
  const Locator loc = it->second;
  index_.erase(it);

  // The index gives the level in O(1); finding the order within the level is a
  // linear scan, since a deque is not addressable by id. This is the baseline's
  // structural cost — the flat implementation will make cancel O(1) with
  // intrusive links — and the reason cancellation is a headline benchmark.
  const auto remove_from_level = [&](auto& book) {
    const auto level_it = book.find(loc.price);
    assert(level_it != book.end() && "index and book out of sync on cancel");
    Level& level = level_it->second;
    for (auto order_it = level.begin(); order_it != level.end(); ++order_it) {
      if (order_it->id == id) {
        level.erase(order_it);
        break;
      }
    }
    if (level.empty()) {
      book.erase(level_it);
    }
  };

  if (is_buy(loc.side)) {
    remove_from_level(bids_);
  } else {
    remove_from_level(asks_);
  }
  return true;
}

std::optional<Price> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.begin()->first;
}

Quantity OrderBook::quantity_at(Side side, Price price) const {
  Quantity total = 0;
  if (is_buy(side)) {
    if (const auto it = bids_.find(price); it != bids_.end()) {
      for (const RestingOrder& o : it->second) {
        total += o.quantity;
      }
    }
  } else {
    if (const auto it = asks_.find(price); it != asks_.end()) {
      for (const RestingOrder& o : it->second) {
        total += o.quantity;
      }
    }
  }
  return total;
}

std::size_t OrderBook::resting_order_count() const noexcept {
  // Every resting order has exactly one index entry, so the index size is the
  // resting-order count without walking the levels.
  return index_.size();
}

bool OrderBook::empty() const noexcept {
  return bids_.empty() && asks_.empty();
}

}  // namespace lob
