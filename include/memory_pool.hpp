#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace ob {

// fixed-block pool with embedded free list
// o(1) alloc/dealloc, no malloc in hot path
template<typename T, size_t Capacity>
class MemPool {
    static_assert(sizeof(T) >= sizeof(void*), "T must fit a pointer");
    static_assert(alignof(T) >= alignof(void*), "T alignment must be >= pointer");

    struct FreeNode { FreeNode* next; };

    alignas(64) std::array<std::byte, sizeof(T) * Capacity> storage_;
    FreeNode* free_head_ = nullptr;
    size_t alloc_cnt_ = 0;

public:
    MemPool() noexcept {
        // build free list in reverse for cache-friendly allocation
        auto* base = reinterpret_cast<std::byte*>(storage_.data());
        for (size_t i = Capacity; i > 0; --i) {
            auto* node = reinterpret_cast<FreeNode*>(base + (i - 1) * sizeof(T));
            node->next = free_head_;
            free_head_ = node;
        }
    }

    MemPool(const MemPool&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    MemPool(MemPool&&) = delete;
    MemPool& operator=(MemPool&&) = delete;

    // o(1) allocation - pop from free list
    [[nodiscard]] T* alloc() noexcept {
        if (free_head_ == nullptr) [[unlikely]] {
            return nullptr;
        }
        auto* node = free_head_;
        free_head_ = node->next;
        ++alloc_cnt_;
        return std::launder(reinterpret_cast<T*>(node));
    }

    // o(1) deallocation - push to free list
    void dealloc(T* p) noexcept {
        if (p == nullptr) [[unlikely]] return;
        p->~T();
        auto* node = reinterpret_cast<FreeNode*>(p);
        node->next = free_head_;
        free_head_ = node;
        --alloc_cnt_;
    }

    template<typename... Args>
    [[nodiscard]] T* create(Args&&... args) noexcept {
        T* p = alloc();
        if (p) [[likely]] {
            ::new (static_cast<void*>(p)) T{std::forward<Args>(args)...};
        }
        return p;
    }

    [[nodiscard]] size_t used() const noexcept { return alloc_cnt_; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return Capacity; }
    [[nodiscard]] size_t available() const noexcept { return Capacity - alloc_cnt_; }
    [[nodiscard]] bool full() const noexcept { return alloc_cnt_ == Capacity; }
    [[nodiscard]] bool empty() const noexcept { return alloc_cnt_ == 0; }

    [[nodiscard]] bool owns(const T* p) const noexcept {
        auto* base = storage_.data();
        auto* ptr = reinterpret_cast<const std::byte*>(p);
        return ptr >= base && ptr < base + sizeof(T) * Capacity;
    }
};

} // namespace ob
