#pragma once

#include "types.hpp"
#include "order.hpp"
#include "price_level.hpp"
#include "memory_pool.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ob {

// execution report for matching
struct Fill {
    OrderId passive_id;
    Qty qty;
    Price price;
};

// result of add operation
enum class AddResult : uint8_t {
    Ok = 0,
    DuplicateId,
    InvalidPrice,
    PoolExhausted,
    InvalidQty
};

// high-performance order book
// array-indexed price levels, o(1) operations
template<int64_t MaxPrice = DEFAULT_MAX_PRICE, size_t MaxOrders = DEFAULT_MAX_ORDERS>
class OrderBook {
    // array-based price level index
    std::array<PriceLevel, MaxPrice + 1> levels_{};

    // best price tracking - hot data together
    Price best_bid_{NO_BID};
    Price best_ask_{Price{MaxPrice + 1}};
    size_t total_orders_ = 0;

    // memory pool for orders
    MemPool<Order, MaxOrders> pool_;

    // direct-mapped order lookup: slot = id % maxorders
    // o(1) for sequential ids, handles collisions via linear probe
    std::array<Order*, MaxOrders> order_map_{};

    // hash: simple modulo - perfect for sequential ids
    [[nodiscard]] static constexpr size_t slot(OrderId id) noexcept {
        return static_cast<size_t>(id.raw() % MaxOrders);
    }

    // o(1) lookup for sequential ids, o(k) for k collisions
    [[nodiscard]] Order* lookup(OrderId id) const noexcept {
        size_t idx = slot(id);
        Order* o = order_map_[idx];
        if (o != nullptr && o->id == id) [[likely]] {
            return o;
        }
        // linear probe for collision case
        size_t start = idx;
        idx = (idx + 1) % MaxOrders;
        while (idx != start) {
            o = order_map_[idx];
            if (o == nullptr) return nullptr;
            if (o->id == id) return o;
            idx = (idx + 1) % MaxOrders;
        }
        return nullptr;
    }

    // insert into map
    bool insert_map(Order* o) noexcept {
        size_t idx = slot(o->id);
        size_t start = idx;
        while (order_map_[idx] != nullptr) {
            if (order_map_[idx]->id == o->id) [[unlikely]] {
                return false;  // duplicate
            }
            idx = (idx + 1) % MaxOrders;
            if (idx == start) [[unlikely]] return false;  // full
        }
        order_map_[idx] = o;
        return true;
    }

    // remove from map with backward shift deletion
    void remove_map(OrderId id) noexcept {
        size_t idx = slot(id);
        // find the order
        while (order_map_[idx] != nullptr) {
            if (order_map_[idx]->id == id) {
                // found - remove and fix probe chain
                order_map_[idx] = nullptr;
                // rehash subsequent entries
                size_t next = (idx + 1) % MaxOrders;
                while (order_map_[next] != nullptr) {
                    Order* to_rehash = order_map_[next];
                    order_map_[next] = nullptr;
                    insert_map(to_rehash);  // re-insert at proper position
                    next = (next + 1) % MaxOrders;
                }
                return;
            }
            idx = (idx + 1) % MaxOrders;
        }
    }

    // update best bid after removal/match
    void update_best_bid() noexcept {
        while (best_bid_.raw() >= 0 && levels_[best_bid_.raw()].empty()) [[unlikely]] {
            --best_bid_;
        }
    }

    // update best ask after removal/match
    void update_best_ask() noexcept {
        while (best_ask_.raw() <= MaxPrice && levels_[best_ask_.raw()].empty()) [[unlikely]] {
            ++best_ask_;
        }
    }

    // remove order from book and pool
    void remove_from_book(Order* o) noexcept {
        levels_[o->price.raw()].remove(o);
        remove_map(o->id);
        pool_.dealloc(o);
        --total_orders_;
    }

public:
    OrderBook() noexcept = default;

    // add limit order
    [[nodiscard]] AddResult add(OrderId id, Side side, Price px, Qty qty,
                                 OrdType type = OrdType::Limit,
                                 Timestamp ts = Timestamp{0}) noexcept {
        if (lookup(id) != nullptr) [[unlikely]] return AddResult::DuplicateId;
        if (qty.raw() <= 0) [[unlikely]] return AddResult::InvalidQty;
        if (px.raw() < 0 || px.raw() > MaxPrice) [[unlikely]] return AddResult::InvalidPrice;

        // match if crossing
        Qty remaining = qty;
        if (side == Side::Buy) {
            if (px >= best_ask_) [[unlikely]] {
                remaining = match_internal(side, remaining, px);
            }
        } else {
            if (px <= best_bid_) [[unlikely]] {
                remaining = match_internal(side, remaining, px);
            }
        }

        // ioc/market don't rest
        if (type == OrdType::IOC || type == OrdType::Market) [[unlikely]] {
            return AddResult::Ok;
        }

        // fully matched
        if (remaining.raw() <= 0) [[unlikely]] {
            return AddResult::Ok;
        }

        // allocate from pool
        Order* o = pool_.alloc();
        if (o == nullptr) [[unlikely]] return AddResult::PoolExhausted;

        // construct
        ::new (static_cast<void*>(o)) Order{id, px, remaining, side, type, ts};

        // insert to map
        if (!insert_map(o)) [[unlikely]] {
            pool_.dealloc(o);
            return AddResult::DuplicateId;
        }

        // add to price level
        levels_[px.raw()].push_back(o);
        ++total_orders_;

        // update best
        if (side == Side::Buy) {
            if (px > best_bid_) [[likely]] best_bid_ = px;
        } else {
            if (px < best_ask_) [[likely]] best_ask_ = px;
        }

        return AddResult::Ok;
    }

    // cancel order - o(1) for sequential ids
    bool cancel(OrderId id) noexcept {
        Order* o = lookup(id);
        if (o == nullptr) [[unlikely]] return false;

        Price px = o->price;
        Side side = o->side;

        remove_from_book(o);

        // update best only if at best
        if (side == Side::Buy) {
            if (px == best_bid_) [[unlikely]] update_best_bid();
        } else {
            if (px == best_ask_) [[unlikely]] update_best_ask();
        }

        return true;
    }

    // market order
    [[nodiscard]] Qty match(Side aggressor, Qty qty) noexcept {
        return match_internal(aggressor, qty,
            aggressor == Side::Buy ? Price{MaxPrice} : Price{0});
    }

private:
    [[nodiscard]] Qty match_internal(Side aggressor, Qty qty, Price limit) noexcept {
        if (aggressor == Side::Buy) {
            while (qty.raw() > 0 && best_ask_.raw() <= limit.raw() &&
                   best_ask_.raw() <= MaxPrice) [[likely]] {
                PriceLevel& level = levels_[best_ask_.raw()];
                qty = match_level(level, qty);
                if (level.empty()) [[unlikely]] update_best_ask();
            }
        } else {
            while (qty.raw() > 0 && best_bid_.raw() >= limit.raw() &&
                   best_bid_.raw() >= 0) [[likely]] {
                PriceLevel& level = levels_[best_bid_.raw()];
                qty = match_level(level, qty);
                if (level.empty()) [[unlikely]] update_best_bid();
            }
        }
        return qty;
    }

    [[nodiscard]] Qty match_level(PriceLevel& level, Qty qty) noexcept {
        while (qty.raw() > 0 && !level.empty()) [[likely]] {
            Order* o = level.front();

            // prefetch next order
            if (o->next != level.end()) [[likely]] {
                __builtin_prefetch(o->next, 0, 3);
            }

            Qty fill = std::min(qty, o->qty);
            o->fill(fill);
            qty -= fill;
            level.reduce_qty(fill);

            if (o->filled()) [[likely]] {
                remove_from_book(o);
            }
        }
        return qty;
    }

public:
    // accessors
    [[nodiscard]] Price bid() const noexcept { return best_bid_; }
    [[nodiscard]] Price ask() const noexcept { return best_ask_; }

    [[nodiscard]] Qty bid_qty() const noexcept {
        if (best_bid_.raw() < 0) [[unlikely]] return Qty{0};
        return levels_[best_bid_.raw()].qty();
    }

    [[nodiscard]] Qty ask_qty() const noexcept {
        if (best_ask_.raw() > MaxPrice) [[unlikely]] return Qty{0};
        return levels_[best_ask_.raw()].qty();
    }

    [[nodiscard]] Price spread() const noexcept { return best_ask_ - best_bid_; }
    [[nodiscard]] bool has_bid() const noexcept { return best_bid_.raw() >= 0; }
    [[nodiscard]] bool has_ask() const noexcept { return best_ask_.raw() <= MaxPrice; }
    [[nodiscard]] bool crossed() const noexcept { return has_bid() && has_ask() && best_bid_ >= best_ask_; }
    [[nodiscard]] size_t order_count() const noexcept { return total_orders_; }
    [[nodiscard]] size_t pool_used() const noexcept { return pool_.used(); }
    [[nodiscard]] size_t pool_capacity() const noexcept { return pool_.capacity(); }

    [[nodiscard]] const Order* get_order(OrderId id) const noexcept { return lookup(id); }
    [[nodiscard]] const PriceLevel& level_at(Price px) const noexcept { return levels_[px.raw()]; }

    static constexpr int64_t max_price() noexcept { return MaxPrice; }
    static constexpr size_t max_orders() noexcept { return MaxOrders; }
};

} // namespace ob
