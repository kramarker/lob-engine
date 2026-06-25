#include "lob/order_book.hpp"

namespace lob {

std::vector<Fill> OrderBook::add_limit_order(Order order) {
  std::vector<Fill> fills;

  // No matching yet: any positive quantity simply rests at its limit price,
  // joining the back of its price level (later arrivals have lower priority).
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
