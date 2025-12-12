#include "order_book.hpp"
#include <cassert>
#include <cstdio>

using namespace ob;

// Small book for tests
using TestBook = OrderBook<10000, 1000>;

void test_empty_book() {
    TestBook book;

    assert(!book.has_bid());
    assert(!book.has_ask());
    assert(book.bid().raw() == -1);
    assert(book.ask().raw() == 10001);
    assert(book.bid_qty().raw() == 0);
    assert(book.ask_qty().raw() == 0);
    assert(book.order_count() == 0);
    assert(!book.crossed());

    printf("[PASS] empty_book\n");
}

void test_single_bid() {
    TestBook book;

    auto res = book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10});
    assert(res == AddResult::Ok);

    assert(book.has_bid());
    assert(!book.has_ask());
    assert(book.bid().raw() == 100);
    assert(book.bid_qty().raw() == 10);
    assert(book.order_count() == 1);

    printf("[PASS] single_bid\n");
}

void test_single_ask() {
    TestBook book;

    auto res = book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10});
    assert(res == AddResult::Ok);

    assert(!book.has_bid());
    assert(book.has_ask());
    assert(book.ask().raw() == 100);
    assert(book.ask_qty().raw() == 10);
    assert(book.order_count() == 1);

    printf("[PASS] single_ask\n");
}

void test_best_bid_ask_tracking() {
    TestBook book;

    // Add multiple bids - best should be highest
    assert(book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Buy, Price{102}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{3}, Side::Buy, Price{101}, Qty{10}) == AddResult::Ok);

    assert(book.bid().raw() == 102);

    // Add multiple asks - best should be lowest
    assert(book.add(OrderId{4}, Side::Sell, Price{110}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{5}, Side::Sell, Price{108}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{6}, Side::Sell, Price{109}, Qty{10}) == AddResult::Ok);

    assert(book.ask().raw() == 108);
    assert(book.spread().raw() == 6);

    printf("[PASS] best_bid_ask_tracking\n");
}

void test_cancel_order() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Buy, Price{100}, Qty{20}) == AddResult::Ok);

    assert(book.bid_qty().raw() == 30);
    assert(book.order_count() == 2);

    bool cancelled = book.cancel(OrderId{1});
    assert(cancelled);
    assert(book.bid_qty().raw() == 20);
    assert(book.order_count() == 1);

    // Cancel non-existent
    cancelled = book.cancel(OrderId{99});
    assert(!cancelled);

    printf("[PASS] cancel_order\n");
}

void test_cancel_updates_best() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Buy, Price{102}, Qty{10}) == AddResult::Ok);

    assert(book.bid().raw() == 102);

    book.cancel(OrderId{2});
    assert(book.bid().raw() == 100);

    book.cancel(OrderId{1});
    assert(!book.has_bid());

    printf("[PASS] cancel_updates_best\n");
}

void test_price_time_priority() {
    TestBook book;

    // Add 3 orders at same price
    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{3}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);

    // Market buy should fill order 1 first
    Qty remaining = book.match(Side::Buy, Qty{15});
    assert(remaining.raw() == 0);

    // Order 1 should be gone, order 2 partially filled
    assert(book.get_order(OrderId{1}) == nullptr);
    const Order* o2 = book.get_order(OrderId{2});
    assert(o2 != nullptr);
    assert(o2->qty.raw() == 5);  // 10 - 5 = 5 remaining
    assert(book.get_order(OrderId{3}) != nullptr);

    printf("[PASS] price_time_priority\n");
}

void test_partial_fill() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{100}) == AddResult::Ok);

    Qty remaining = book.match(Side::Buy, Qty{30});
    assert(remaining.raw() == 0);

    const Order* o = book.get_order(OrderId{1});
    assert(o != nullptr);
    assert(o->qty.raw() == 70);
    assert(o->orig_qty.raw() == 100);
    assert(book.ask_qty().raw() == 70);

    printf("[PASS] partial_fill\n");
}

void test_full_fill() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{50}) == AddResult::Ok);

    Qty remaining = book.match(Side::Buy, Qty{50});
    assert(remaining.raw() == 0);

    assert(book.get_order(OrderId{1}) == nullptr);
    assert(!book.has_ask());
    assert(book.order_count() == 0);

    printf("[PASS] full_fill\n");
}

void test_crossing_order() {
    TestBook book;

    // Resting ask at 100
    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);

    // Aggressive bid at 100 should match immediately
    auto res = book.add(OrderId{2}, Side::Buy, Price{100}, Qty{5});
    assert(res == AddResult::Ok);

    // Ask should be partially filled
    const Order* ask = book.get_order(OrderId{1});
    assert(ask != nullptr);
    assert(ask->qty.raw() == 5);

    // Aggressive bid should not rest (fully matched)
    assert(book.get_order(OrderId{2}) == nullptr);

    printf("[PASS] crossing_order\n");
}

void test_ioc_order() {
    TestBook book;

    // Resting ask at 100 with qty 5
    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{5}) == AddResult::Ok);

    // IOC buy for 10 - should match 5, cancel rest
    auto res = book.add(OrderId{2}, Side::Buy, Price{100}, Qty{10}, OrdType::IOC);
    assert(res == AddResult::Ok);

    // Ask should be fully filled
    assert(book.get_order(OrderId{1}) == nullptr);

    // IOC should not rest
    assert(book.get_order(OrderId{2}) == nullptr);

    // Only 0 orders remain
    assert(book.order_count() == 0);

    printf("[PASS] ioc_order\n");
}

void test_market_order() {
    TestBook book;

    // Resting asks at different prices
    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Sell, Price{101}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{3}, Side::Sell, Price{102}, Qty{10}) == AddResult::Ok);

    // Market buy for 25 - should fill all at 100, all at 101, 5 at 102
    Qty remaining = book.match(Side::Buy, Qty{25});
    assert(remaining.raw() == 0);

    assert(book.get_order(OrderId{1}) == nullptr);
    assert(book.get_order(OrderId{2}) == nullptr);
    const Order* o3 = book.get_order(OrderId{3});
    assert(o3 != nullptr);
    assert(o3->qty.raw() == 5);

    assert(book.ask().raw() == 102);

    printf("[PASS] market_order\n");
}

void test_market_order_insufficient_liquidity() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);

    // Market buy for more than available
    Qty remaining = book.match(Side::Buy, Qty{100});
    assert(remaining.raw() == 90);  // 100 - 10 = 90 unfilled

    assert(!book.has_ask());

    printf("[PASS] market_order_insufficient_liquidity\n");
}

void test_invariant_best_bid_less_than_ask() {
    TestBook book;

    assert(book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{2}, Side::Sell, Price{105}, Qty{10}) == AddResult::Ok);

    assert(book.bid().raw() < book.ask().raw());
    assert(!book.crossed());

    // Add more orders
    assert(book.add(OrderId{3}, Side::Buy, Price{102}, Qty{10}) == AddResult::Ok);
    assert(book.add(OrderId{4}, Side::Sell, Price{103}, Qty{10}) == AddResult::Ok);

    assert(book.bid().raw() == 102);
    assert(book.ask().raw() == 103);
    assert(!book.crossed());

    printf("[PASS] invariant_best_bid_less_than_ask\n");
}

void test_duplicate_order_id() {
    TestBook book;

    auto res1 = book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10});
    assert(res1 == AddResult::Ok);

    auto res2 = book.add(OrderId{1}, Side::Sell, Price{105}, Qty{10});
    assert(res2 == AddResult::DuplicateId);

    assert(book.order_count() == 1);

    printf("[PASS] duplicate_order_id\n");
}

void test_invalid_price() {
    TestBook book;

    auto res1 = book.add(OrderId{1}, Side::Buy, Price{-1}, Qty{10});
    assert(res1 == AddResult::InvalidPrice);

    auto res2 = book.add(OrderId{2}, Side::Buy, Price{10001}, Qty{10});
    assert(res2 == AddResult::InvalidPrice);

    assert(book.order_count() == 0);

    printf("[PASS] invalid_price\n");
}

void test_invalid_qty() {
    TestBook book;

    auto res1 = book.add(OrderId{1}, Side::Buy, Price{100}, Qty{0});
    assert(res1 == AddResult::InvalidQty);

    auto res2 = book.add(OrderId{2}, Side::Buy, Price{100}, Qty{-5});
    assert(res2 == AddResult::InvalidQty);

    assert(book.order_count() == 0);

    printf("[PASS] invalid_qty\n");
}

void test_multiple_price_levels() {
    TestBook book;

    // Build a book with multiple levels
    for (int i = 0; i < 10; ++i) {
        assert(book.add(OrderId{static_cast<uint64_t>(i)}, Side::Buy,
                        Price{100 - i}, Qty{10}) == AddResult::Ok);
        assert(book.add(OrderId{static_cast<uint64_t>(100 + i)}, Side::Sell,
                        Price{110 + i}, Qty{10}) == AddResult::Ok);
    }

    assert(book.bid().raw() == 100);
    assert(book.ask().raw() == 110);
    assert(book.order_count() == 20);

    // Cancel all bids
    for (int i = 0; i < 10; ++i) {
        book.cancel(OrderId{static_cast<uint64_t>(i)});
    }

    assert(!book.has_bid());
    assert(book.has_ask());

    printf("[PASS] multiple_price_levels\n");
}

void test_pool_reuse() {
    TestBook book;

    // Add and cancel many orders
    for (uint64_t i = 0; i < 100; ++i) {
        assert(book.add(OrderId{i}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    }
    assert(book.pool_used() == 100);

    for (uint64_t i = 0; i < 100; ++i) {
        book.cancel(OrderId{i});
    }
    assert(book.pool_used() == 0);

    // Pool should be reusable
    for (uint64_t i = 100; i < 200; ++i) {
        assert(book.add(OrderId{i}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);
    }
    assert(book.pool_used() == 100);

    printf("[PASS] pool_reuse\n");
}

void test_aggressive_bid_price_improvement() {
    TestBook book;

    // Resting ask at 100
    assert(book.add(OrderId{1}, Side::Sell, Price{100}, Qty{10}) == AddResult::Ok);

    // Aggressive bid at 105 (above best ask) should match at 100
    auto res = book.add(OrderId{2}, Side::Buy, Price{105}, Qty{5});
    assert(res == AddResult::Ok);

    // Ask partially filled
    const Order* ask = book.get_order(OrderId{1});
    assert(ask != nullptr);
    assert(ask->qty.raw() == 5);

    printf("[PASS] aggressive_bid_price_improvement\n");
}

void test_aggressive_ask_price_improvement() {
    TestBook book;

    // Resting bid at 100
    assert(book.add(OrderId{1}, Side::Buy, Price{100}, Qty{10}) == AddResult::Ok);

    // Aggressive ask at 95 (below best bid) should match at 100
    auto res = book.add(OrderId{2}, Side::Sell, Price{95}, Qty{5});
    assert(res == AddResult::Ok);

    // Bid partially filled
    const Order* bid = book.get_order(OrderId{1});
    assert(bid != nullptr);
    assert(bid->qty.raw() == 5);

    printf("[PASS] aggressive_ask_price_improvement\n");
}

int main() {
    printf("=== Order Book Correctness Tests ===\n\n");

    test_empty_book();
    test_single_bid();
    test_single_ask();
    test_best_bid_ask_tracking();
    test_cancel_order();
    test_cancel_updates_best();
    test_price_time_priority();
    test_partial_fill();
    test_full_fill();
    test_crossing_order();
    test_ioc_order();
    test_market_order();
    test_market_order_insufficient_liquidity();
    test_invariant_best_bid_less_than_ask();
    test_duplicate_order_id();
    test_invalid_price();
    test_invalid_qty();
    test_multiple_price_levels();
    test_pool_reuse();
    test_aggressive_bid_price_improvement();
    test_aggressive_ask_price_improvement();

    printf("\n=== All tests passed ===\n");
    return 0;
}
