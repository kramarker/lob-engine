#include <gtest/gtest.h>

#include "lob/order_book.hpp"

namespace {

using lob::OrderBook;
using lob::Side;

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

}  // namespace
