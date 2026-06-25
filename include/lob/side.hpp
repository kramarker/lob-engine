#pragma once

#include <cstdint>

namespace lob {

// Which side of the book an order sits on.
enum class Side : std::uint8_t { Buy, Sell };

// The side that an order of side `s` matches against.
constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr bool is_buy(Side s) noexcept {
  return s == Side::Buy;
}

constexpr bool is_sell(Side s) noexcept {
  return s == Side::Sell;
}

constexpr const char* to_string(Side s) noexcept {
  return s == Side::Buy ? "Buy" : "Sell";
}

}  // namespace lob
