#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <type_traits>

namespace ob {

// zero-cost wrapper via spaceship operator
template<typename T, typename Tag>
struct Strong {
    T val;

    constexpr Strong() noexcept : val{} {}
    constexpr explicit Strong(T v) noexcept : val(v) {}

    constexpr T raw() const noexcept { return val; }

    constexpr auto operator<=>(const Strong&) const noexcept = default;
    constexpr bool operator==(const Strong&) const noexcept = default;

    constexpr Strong& operator+=(Strong rhs) noexcept { val += rhs.val; return *this; }
    constexpr Strong& operator-=(Strong rhs) noexcept { val -= rhs.val; return *this; }

    constexpr Strong operator+(Strong rhs) const noexcept { return Strong{val + rhs.val}; }
    constexpr Strong operator-(Strong rhs) const noexcept { return Strong{val - rhs.val}; }

    constexpr Strong& operator++() noexcept { ++val; return *this; }
    constexpr Strong& operator--() noexcept { --val; return *this; }
};

using OrderId   = Strong<uint64_t, struct OrderIdTag>;
using Price     = Strong<int64_t, struct PriceTag>;      // fixed-point ticks
using Qty       = Strong<int64_t, struct QtyTag>;
using Timestamp = Strong<uint64_t, struct TimestampTag>;

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class OrdType : uint8_t { Limit = 0, Market = 1, IOC = 2 };

constexpr Side flip(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr bool is_buy(Side s) noexcept { return s == Side::Buy; }
constexpr bool is_sell(Side s) noexcept { return s == Side::Sell; }

inline constexpr int64_t DEFAULT_MAX_PRICE = 1'000'000;
inline constexpr size_t DEFAULT_MAX_ORDERS = 10'000'000;

// invalid price sentinels
inline constexpr Price NO_BID{-1};
inline constexpr Price NO_ASK{DEFAULT_MAX_PRICE + 1};

// verify zero-cost
static_assert(sizeof(OrderId) == sizeof(uint64_t));
static_assert(sizeof(Price) == sizeof(int64_t));
static_assert(sizeof(Qty) == sizeof(int64_t));
static_assert(sizeof(Side) == 1);
static_assert(sizeof(OrdType) == 1);

static_assert(std::is_trivially_copyable_v<OrderId>);
static_assert(std::is_trivially_copyable_v<Price>);
static_assert(std::is_trivially_copyable_v<Qty>);

} // namespace ob
