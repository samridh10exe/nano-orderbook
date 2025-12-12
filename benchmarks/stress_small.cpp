#include "order_book.hpp"
#include "workload.hpp"
#include <cstdio>
#include <chrono>
#include <memory>

using namespace ob;

static constexpr size_t STRESS_OPS = 10'000'000;  // 10M ops

int main() {
    printf("=== Order Book Stress Test (10M) ===\n\n");

    using Book = OrderBook<100000, 1'000'000>;
    auto book = std::make_unique<Book>();

    WorkloadGen gen(12345, 1000.0, 50000, 200.0, 0.40, 0.25, 0.05);

    auto start = std::chrono::steady_clock::now();

    size_t add_cnt = 0, cancel_cnt = 0, match_cnt = 0;
    size_t add_ok = 0, cancel_ok = 0;

    for (size_t i = 0; i < STRESS_OPS; ++i) {
        auto op = gen.next();

        switch (op.type) {
            case OpType::Add: {
                ++add_cnt;
                auto res = book->add(op.id, op.side, op.price, op.qty, op.ord_type);
                if (res == AddResult::Ok) ++add_ok;
                break;
            }
            case OpType::Cancel: {
                ++cancel_cnt;
                if (book->cancel(op.id)) ++cancel_ok;
                break;
            }
            case OpType::Match: {
                ++match_cnt;
                (void)book->match(op.side, op.qty);
                break;
            }
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    printf("Time: %.2f sec\n", elapsed_ms / 1000.0);
    printf("Throughput: %.2f M ops/sec\n",
           static_cast<double>(STRESS_OPS) / (elapsed_ms / 1000.0) / 1e6);

    printf("\nOperation breakdown:\n");
    printf("  Add:    %zu (success: %zu, %.1f%%)\n",
           add_cnt, add_ok, 100.0 * static_cast<double>(add_ok) / add_cnt);
    printf("  Cancel: %zu (success: %zu, %.1f%%)\n",
           cancel_cnt, cancel_ok, 100.0 * static_cast<double>(cancel_ok) / cancel_cnt);
    printf("  Match:  %zu\n", match_cnt);

    printf("\nFinal book state:\n");
    printf("  Orders: %zu\n", book->order_count());
    printf("  Pool used: %zu / %zu\n", book->pool_used(), book->pool_capacity());

    // Memory check: pool_used should equal order_count
    bool mem_ok = (book->pool_used() == book->order_count());
    printf("\nMemory check: %s\n", mem_ok ? "PASS" : "FAIL");

    if (book->has_bid() && book->has_ask()) {
        printf("  Bid: %ld, Ask: %ld, Spread: %ld\n",
               book->bid().raw(), book->ask().raw(), book->spread().raw());
    }

    printf("\n=== Stress test %s ===\n", mem_ok ? "PASSED" : "FAILED");
    return mem_ok ? 0 : 1;
}
