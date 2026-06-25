#include "lob/order_book.hpp"

namespace lob {

std::vector<Fill> OrderBook::add_limit_order(Order order) {
  std::vector<Fill> fills;

  if (is_buy(order.side)) {
    // A buy crosses the asks from the lowest price upward, while its limit is at
    // or above the ask price, filling resting orders in full and then partially
    // filling the next resting order with whatever quantity remains.
    while (order.quantity > 0 && !asks_.empty()) {
      const auto best = asks_.begin();
      const Price ask_price = best->first;
      if (order.price < ask_price) {
        break;  // best ask is above our limit: no longer crossing.
      }
      Level& level = best->second;
      while (!level.empty() && order.quantity >= level.front().quantity) {
        const RestingOrder& maker = level.front();
        fills.push_back({order.id, maker.id, ask_price, maker.quantity});
        order.quantity -= maker.quantity;
        level.pop_front();
      }
      if (level.empty()) {
        asks_.erase(best);
      } else if (order.quantity > 0) {
        // The incoming order is smaller than the oldest resting order at this
        // price: fill it fully and leave the resting order with the remainder.
        RestingOrder& maker = level.front();
        fills.push_back({order.id, maker.id, ask_price, order.quantity});
        maker.quantity -= order.quantity;
        order.quantity = 0;
      }
    }
  } else {
    // A sell crosses the bids from the highest price downward, while its limit
    // is at or below the bid price, with the same fill behaviour as the buy
    // path (full fills, then a partial fill of the next resting order).
    while (order.quantity > 0 && !bids_.empty()) {
      const auto best = bids_.begin();
      const Price bid_price = best->first;
      if (order.price > bid_price) {
        break;  // best bid is below our limit: no longer crossing.
      }
      Level& level = best->second;
      while (!level.empty() && order.quantity >= level.front().quantity) {
        const RestingOrder& maker = level.front();
        fills.push_back({order.id, maker.id, bid_price, maker.quantity});
        order.quantity -= maker.quantity;
        level.pop_front();
      }
      if (level.empty()) {
        bids_.erase(best);
      } else if (order.quantity > 0) {
        // The incoming order is smaller than the oldest resting order at this
        // price: fill it fully and leave the resting order with the remainder.
        RestingOrder& maker = level.front();
        fills.push_back({order.id, maker.id, bid_price, order.quantity});
        maker.quantity -= order.quantity;
        order.quantity = 0;
      }
    }
  }

  // Any quantity that did not cross rests at its limit price, joining the back
  // of its price level (later arrivals have lower priority).
  if (order.quantity > 0) {
    const RestingOrder resting{order.id, order.quantity, next_seq_++};
    if (is_buy(order.side)) {
      bids_[order.price].push_back(resting);
    } else {
      asks_[order.price].push_back(resting);
    }
  }

  return fills;
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

bool OrderBook::empty() const noexcept {
  return bids_.empty() && asks_.empty();
}

}  // namespace lob
