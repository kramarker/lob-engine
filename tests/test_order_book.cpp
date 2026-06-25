#include <gtest/gtest.h>

#include <vector>

#include "lob/order_book.hpp"

namespace {

using lob::Fill;
using lob::Order;
using lob::OrderBook;
using lob::OrderType;
using lob::Side;
using lob::TimeInForce;

// Convenience constructor for a GTC limit order — the common case in these
// tests. Type and time-in-force are set explicitly per order where they matter.
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
  const std::vector<Fill> fills = book.submit(limit(1, Side::Buy, 100, 10));
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
  const std::vector<Fill> fills = book.submit(limit(1, Side::Sell, 105, 7));
  EXPECT_TRUE(fills.empty());
  ASSERT_TRUE(book.best_ask().has_value());
  EXPECT_EQ(*book.best_ask(), 105);
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_EQ(book.quantity_at(Side::Sell, 105), 7u);
}

// A bid strictly below the ask does not cross: both orders coexist.
TEST(RestingOrders, NonCrossingBidAndAskCoexist) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 99, 5));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Sell, 101, 5));
  EXPECT_TRUE(fills.empty());
  EXPECT_EQ(*book.best_bid(), 99);
  EXPECT_EQ(*book.best_ask(), 101);
}

// Multiple resting orders at one price accumulate into that level's quantity.
TEST(RestingOrders, SamePriceAccumulatesQuantity) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 10));
  book.submit(limit(2, Side::Buy, 100, 15));
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 25u);
}

// A buy that exactly matches a resting sell trades in full; nothing rests, and
// the single fill names taker, maker, price and size.
TEST(FullMatches, BuyFullyMatchesRestingSell) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 10));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Buy, 100, 10));
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
  book.submit(limit(1, Side::Buy, 100, 8));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Sell, 100, 8));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].quantity, 8u);
  EXPECT_TRUE(book.empty());
}

// A buy priced above the ask executes at the resting maker's price (the price
// improvement accrues to the aggressing taker).
TEST(FullMatches, AggressiveBuyTradesAtMakerPrice) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 5));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Buy, 105, 5));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_TRUE(book.empty());
}

// A large buy sweeps multiple ask levels cheapest-first, emitting one fill per
// resting order consumed.
TEST(FullMatches, BuySweepsMultipleAskLevels) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 4));
  book.submit(limit(2, Side::Sell, 101, 6));
  const std::vector<Fill> fills = book.submit(limit(3, Side::Buy, 101, 10));
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
  book.submit(limit(1, Side::Sell, 100, 10));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Buy, 100, 4));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 6u);
  EXPECT_FALSE(book.best_bid().has_value());
}

// An incoming buy larger than the resting sell consumes it fully and rests its
// own remainder as a new bid.
TEST(PartialFills, IncomingLargerThanRestingMaker) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 4));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Buy, 100, 10));
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
  book.submit(limit(1, Side::Buy, 100, 10));
  const std::vector<Fill> fills = book.submit(limit(2, Side::Sell, 100, 3));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 3u);
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 7u);
  EXPECT_FALSE(book.best_ask().has_value());
}

// At one price the oldest resting order (lowest sequence) is matched first.
TEST(FifoPriority, OldestOrderAtPriceFillsFirst) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 5));  // rests first
  book.submit(limit(2, Side::Sell, 100, 5));  // rests behind id 1
  const std::vector<Fill> fills = book.submit(limit(3, Side::Buy, 100, 5));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 5u);  // only id 2 remains
}

// A taker sweeping a whole level fills the resting orders in arrival order.
TEST(FifoPriority, SweepFillsInArrivalOrder) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 3));
  book.submit(limit(2, Side::Buy, 100, 3));
  book.submit(limit(3, Side::Buy, 100, 3));
  const std::vector<Fill> fills = book.submit(limit(4, Side::Sell, 100, 9));
  ASSERT_EQ(fills.size(), 3u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(fills[1].maker_id, 2u);
  EXPECT_EQ(fills[2].maker_id, 3u);
  EXPECT_TRUE(book.empty());
}

// A partial fill consumes only the oldest order; a later same-price order keeps
// its place and quantity.
TEST(FifoPriority, PartialFillConsumesOldestOnly) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 5));
  book.submit(limit(2, Side::Sell, 100, 5));
  const std::vector<Fill> fills = book.submit(limit(3, Side::Buy, 100, 3));
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(fills[0].quantity, 3u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 7u);  // 2 left of id 1 + 5 of id 2
}

// --- Cancellation -----------------------------------------------------------

// Cancelling a resting order removes it and frees its price level.
TEST(Cancellation, RemovesRestingOrder) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 10));
  EXPECT_TRUE(book.cancel(1));
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_EQ(book.resting_order_count(), 0u);
}

// Cancelling an unknown id is a no-op that reports failure.
TEST(Cancellation, UnknownIdIsNoOp) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 10));
  EXPECT_FALSE(book.cancel(999));
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 10u);
  EXPECT_EQ(book.resting_order_count(), 1u);
}

// Cancelling one order at a shared price leaves the others, preserving FIFO
// order among the survivors.
TEST(Cancellation, RemovesOnlyTargetAtSharedPrice) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 5));
  book.submit(limit(2, Side::Buy, 100, 5));
  book.submit(limit(3, Side::Buy, 100, 5));
  EXPECT_TRUE(book.cancel(2));
  EXPECT_EQ(book.quantity_at(Side::Buy, 100), 10u);

  // The remaining orders still match oldest-first: id 1 before id 3.
  const std::vector<Fill> fills = book.submit(limit(4, Side::Sell, 100, 10));
  ASSERT_EQ(fills.size(), 2u);
  EXPECT_EQ(fills[0].maker_id, 1u);
  EXPECT_EQ(fills[1].maker_id, 3u);
}

// An order that has fully traded is gone from the index, so cancelling it fails.
TEST(Cancellation, FullyFilledOrderCannotBeCancelled) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 5));
  book.submit(limit(2, Side::Buy, 100, 5));  // consumes id 1
  EXPECT_FALSE(book.cancel(1));
}

// A partially filled maker is still resting and remains cancellable; cancelling
// it removes its remaining quantity.
TEST(Cancellation, PartiallyFilledMakerStillCancellable) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 10));
  book.submit(limit(2, Side::Buy, 100, 4));  // leaves 6 of id 1 resting
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 6u);
  EXPECT_TRUE(book.cancel(1));
  EXPECT_TRUE(book.empty());
}

// After a cancel-and-resubmit cycle the id can be cancelled again, confirming
// the index is cleaned up on both removal paths (match and cancel).
TEST(Cancellation, IndexStaysConsistentAcrossResubmit) {
  OrderBook book;
  book.submit(limit(1, Side::Buy, 100, 5));
  EXPECT_TRUE(book.cancel(1));
  book.submit(limit(1, Side::Buy, 100, 5));
  EXPECT_TRUE(book.cancel(1));
  EXPECT_TRUE(book.empty());
}

// --- Market orders ----------------------------------------------------------

Order market(lob::OrderId id, Side side, lob::Quantity qty) {
  // Price is ignored for market orders; 0 documents that it carries no meaning.
  return Order{id, side, 0, qty, OrderType::Market, TimeInForce::IOC};
}

// A market buy sweeps the asks cheapest-first regardless of price and never
// rests its remainder.
TEST(MarketOrders, BuySweepsAllReachableAsksAndDoesNotRest) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 4));
  book.submit(limit(2, Side::Sell, 102, 4));
  const std::vector<Fill> fills = book.submit(market(3, Side::Buy, 6));
  ASSERT_EQ(fills.size(), 2u);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_EQ(fills[1].price, 102);
  EXPECT_EQ(fills[1].quantity, 2u);
  EXPECT_FALSE(book.best_bid().has_value());  // remainder did not rest
  EXPECT_EQ(book.quantity_at(Side::Sell, 102), 2u);
}

// A market order against an empty opposite side simply produces no fills.
TEST(MarketOrders, AgainstEmptyBookYieldsNoFills) {
  OrderBook book;
  const std::vector<Fill> fills = book.submit(market(1, Side::Buy, 10));
  EXPECT_TRUE(fills.empty());
  EXPECT_TRUE(book.empty());
}

// --- Immediate-or-cancel ----------------------------------------------------

// An IOC limit order takes what it can and discards the rest instead of resting.
TEST(TimeInForce, IocTakesAvailableAndDiscardsRemainder) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 4));
  Order ioc = limit(2, Side::Buy, 100, 10);
  ioc.tif = TimeInForce::IOC;
  const std::vector<Fill> fills = book.submit(ioc);
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0].quantity, 4u);
  EXPECT_FALSE(book.best_bid().has_value());  // 6 unfilled were discarded
  EXPECT_TRUE(book.empty());
}

// --- Fill-or-kill -----------------------------------------------------------

// A FOK order that cannot be filled in full is rejected and leaves the book
// untouched (no partial sweep).
TEST(TimeInForce, FokRejectsWhenNotFullyFillable) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 4));
  Order fok = limit(2, Side::Buy, 100, 10);
  fok.tif = TimeInForce::FOK;
  const std::vector<Fill> fills = book.submit(fok);
  EXPECT_TRUE(fills.empty());
  EXPECT_EQ(book.quantity_at(Side::Sell, 100), 4u);  // resting sell untouched
}

// A FOK order that the book can fill completely executes in full.
TEST(TimeInForce, FokExecutesWhenFullyFillable) {
  OrderBook book;
  book.submit(limit(1, Side::Sell, 100, 6));
  book.submit(limit(2, Side::Sell, 101, 6));
  Order fok = limit(3, Side::Buy, 101, 10);
  fok.tif = TimeInForce::FOK;
  const std::vector<Fill> fills = book.submit(fok);
  ASSERT_EQ(fills.size(), 2u);
  EXPECT_EQ(fills[0].quantity, 6u);
  EXPECT_EQ(fills[1].quantity, 4u);
  EXPECT_EQ(book.quantity_at(Side::Sell, 101), 2u);
}

}  // namespace
