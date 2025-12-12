#pragma once

#include "types.hpp"
#include <cstddef>

namespace ob {

// intrusive list node - exactly 64 bytes (1 cache line)
struct alignas(64) Order {
    // hot - accessed during list ops
    Order* prev = nullptr;  // 8
    Order* next = nullptr;  // 8

    // hot - accessed during lookup/cancel
    OrderId id;             // 8
    Price price;            // 8

    // hot - accessed during matching
    Qty qty;                // 8  remaining
    Qty orig_qty;           // 8  original

    Timestamp ts;           // 8
    Side side;              // 1
    OrdType type;           // 1

    std::byte _pad[6];      // 6

    Order() noexcept = default;

    Order(OrderId id_, Price px_, Qty q_, Side s_, OrdType t_, Timestamp ts_) noexcept
        : prev(nullptr)
        , next(nullptr)
        , id(id_)
        , price(px_)
        , qty(q_)
        , orig_qty(q_)
        , ts(ts_)
        , side(s_)
        , type(t_)
        , _pad{}
    {}

    void fill(Qty amount) noexcept { qty -= amount; }

    [[nodiscard]] bool filled() const noexcept { return qty.raw() <= 0; }
    [[nodiscard]] Qty remaining() const noexcept { return qty; }
};

static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");
static_assert(alignof(Order) == 64, "Order must be cache-line aligned");

} // namespace ob
