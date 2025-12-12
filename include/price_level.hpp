#pragma once

#include "order.hpp"

namespace ob {

// sentinel-based intrusive doubly-linked list
// eliminates null checks in hot path
struct PriceLevel {
    Order sentinel;        // prev = tail, next = head
    size_t order_cnt = 0;
    Qty total_qty{0};

    PriceLevel() noexcept {
        // circular: sentinel points to itself when empty
        sentinel.prev = &sentinel;
        sentinel.next = &sentinel;
    }

    // o(1) append to back (fifo ordering)
    void push_back(Order* o) noexcept {
        Order* tail = sentinel.prev;
        o->prev = tail;
        o->next = &sentinel;
        tail->next = o;
        sentinel.prev = o;
        ++order_cnt;
        total_qty += o->qty;
    }

    // o(1) remove order from list
    void remove(Order* o) noexcept {
        o->prev->next = o->next;
        o->next->prev = o->prev;
        --order_cnt;
        total_qty -= o->qty;
        o->prev = nullptr;
        o->next = nullptr;
    }

    // reduce total qty when order partially filled
    void reduce_qty(Qty amount) noexcept {
        total_qty -= amount;
    }

    // first order (fifo head)
    [[nodiscard]] Order* front() noexcept {
        return sentinel.next;
    }

    [[nodiscard]] const Order* front() const noexcept {
        return sentinel.next;
    }

    // last order
    [[nodiscard]] Order* back() noexcept {
        return sentinel.prev;
    }

    // check if level is empty
    [[nodiscard]] bool empty() const noexcept {
        return order_cnt == 0;
    }

    // sentinel pointer for iteration boundary
    [[nodiscard]] Order* end() noexcept {
        return &sentinel;
    }

    [[nodiscard]] const Order* end() const noexcept {
        return &sentinel;
    }

    [[nodiscard]] size_t count() const noexcept {
        return order_cnt;
    }

    [[nodiscard]] Qty qty() const noexcept {
        return total_qty;
    }
};

} // namespace ob
