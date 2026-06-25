#include <gtest/gtest.h>

#include <vector>

#include "lob/order_book.hpp"

namespace {

using lob::Fill;
using lob::Order;
using lob::OrderBook;
using lob::Side;

// Convenience constructor for a limit order.
Order limit(lob::OrderId id, Side side, lob::Price price, lob::Quantity qty) {
  return Order{id, side, price, qty};
}

// Smoke test that proves the harness is wired up: a fresh book holds nothing
// and reports no best prices.
TEST(OrderBookHarness, FreshBookIsEmpty) {
  OrderBook book;
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 0u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 0u);
}

// A non-crossing buy rests: no fills, and it becomes the best bid.
TEST(RestingOrders, BuyRestsWhenNoCross) {
  OrderBook book;
  const std::vector<Fill> fills = book.add_limit_order(limit(1, Side::Buy, 100, 10));
  EXPECT_TRUE(fills.empty());
  EXPECT_FALSE(book.empty());
  ASSERT_TRUE(book.best_bid().has_value());
  EXPECT_EQ(*book.best_bid(), 100);
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 10u);
}

// A non-crossing sell rests: no fills, and it becomes the best ask.
TEST(RestingOrders, SellRestsWhenNoCross) {
  OrderBook book;
  const std::vector<Fill> fills = book.add_limit_order(limit(1, Side::Sell, 105, 7));
  EXPECT_TRUE(fills.empty());
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(*book.best_ask(), 105);
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_EQ(book.quantity_at(Side::Sell, 105), 7u);
}

// A bid strictly below the ask does not cross: both orders coexist.
TEST(RestingOrders, NonCrossingBidAndAskCoexist) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Buy, 99, 5));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Sell, 101, 5));
  EXPECT_TRUE(fills.empty());
  EXPECT_EQ(*book.best_bid(), 99);
  EXPECT_EQ(*book.best_ask(), 101);
}

// Multiple resting orders at one price accumulate into that level's quantity.
TEST(RestingOrders, SamePriceAccumulatesQuantity) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Buy, 100, 10));
  book.add_limit_order(limit(2, Side::Buy, 100, 15));
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 25u);
}

}  // namespace
