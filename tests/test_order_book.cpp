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

// A buy that exactly matches a resting sell trades in full; nothing rests, and
// the single fill names taker, maker, price and size.
TEST(FullMatches, BuyFullyMatchesRestingSell) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Sell, 100, 10));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Buy, 100, 10));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].taker_id, 2u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].quantity, 10u);
  EXPECT_TRUE(book.empty());
}

// Symmetric case: a sell that exactly matches a resting buy.
TEST(FullMatches, SellFullyMatchesRestingBuy) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Buy, 100, 8));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Sell, 100, 8));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].quantity, 8u);
  EXPECT_TRUE(book.empty());
}

// A buy priced above the ask executes at the resting maker's price (the price
// improvement accrues to the aggressing taker).
TEST(FullMatches, AggressiveBuyTradesAtMakerPrice) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Sell, 100, 5));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Buy, 105, 5));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_TRUE(book.empty());
}

// A large buy sweeps multiple ask levels cheapest-first, emitting one fill per
// resting order consumed.
TEST(FullMatches, BuySweepsMultipleAskLevels) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Sell, 100, 4));
  book.add_limit_order(limit(2, Side::Sell, 101, 6));
  const std::vector<Fill> fills = book.add_limit_order(limit(3, Side::Buy, 101, 10));
  ASSERT_EQ(fills.size(), 2u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_EQ(fills[1].price, 101);
  EXPECT_EQ(fills[1].quantity, 6u);
  EXPECT_TRUE(book.empty());
}

// An incoming buy smaller than the resting sell is fully filled; the resting
// sell keeps the remainder at the same price, and nothing rests on the bid.
TEST(PartialFills, IncomingSmallerThanRestingMaker) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Sell, 100, 10));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Buy, 100, 4));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 6u);
  EXPECT_FALSE(book.best_bid().has_value());
}

// An incoming buy larger than the resting sell consumes it fully and rests its
// own remainder as a new bid.
TEST(PartialFills, IncomingLargerThanRestingMaker) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Sell, 100, 4));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Buy, 100, 10));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_FALSE(book.best_ask().has_value());
  ASSERT_TRUE(book.best_bid().has_value());
  EXPECT_EQ(*book.best_bid(), 100);
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 6u);
}

// Partial fill on the sell side: an incoming sell smaller than the resting buy
// leaves the buy with its remainder.
TEST(PartialFills, SellPartiallyFillsRestingBuy) {
  OrderBook book;
  book.add_limit_order(limit(1, Side::Buy, 100, 10));
  const std::vector<Fill> fills = book.add_limit_order(limit(2, Side::Sell, 100, 3));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 3u);
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 7u);
  EXPECT_FALSE(book.best_ask().has_value());
}

}  // namespace
