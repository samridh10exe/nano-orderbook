#include "order_book.hpp"
#include "timer.hpp"
#include "workload.hpp"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <memory>

using namespace ob;

static constexpr size_t WARMUP_OPS = 10000;
static constexpr size_t BENCH_OPS = 10'000'000;

int main() {
    printf("=== Order Book Benchmark ===\n\n");

    // Get CPU frequency
    double freq_ghz = get_cpu_freq_ghz();
    printf("CPU frequency: %.2f GHz\n", freq_ghz);

    // Create order book on heap (large due to array size)
    using Book = OrderBook<100000, 1'000'000>;
    auto book = std::make_unique<Book>();

    // Generate workload
    printf("Generating %zu operations...\n", WARMUP_OPS + BENCH_OPS);
    WorkloadGen gen(42);
    auto warmup_ops = gen.generate(WARMUP_OPS);
    auto bench_ops = gen.generate(BENCH_OPS);

    // Storage for latencies
    std::vector<uint64_t> add_latencies;
    std::vector<uint64_t> cancel_latencies;
    std::vector<uint64_t> match_latencies;

    add_latencies.reserve(BENCH_OPS);
    cancel_latencies.reserve(BENCH_OPS);
    match_latencies.reserve(BENCH_OPS);

    // Warmup
    printf("Warming up cache (%zu ops)...\n", WARMUP_OPS);
    for (const auto& op : warmup_ops) {
        switch (op.type) {
            case OpType::Add:
                (void)book->add(op.id, op.side, op.price, op.qty, op.ord_type);
                break;
            case OpType::Cancel:
                (void)book->cancel(op.id);
                break;
            case OpType::Match:
                (void)book->match(op.side, op.qty);
                break;
        }
    }

    // Reset book for benchmark (recreate)
    book = std::make_unique<Book>();
    gen.reset(42);
    (void)gen.generate(WARMUP_OPS);  // Skip warmup IDs

    // Benchmark
    printf("Running benchmark (%zu ops)...\n\n", BENCH_OPS);

    uint64_t total_start = rdtsc_start();

    for (const auto& op : bench_ops) {
        uint64_t start = rdtsc();

        switch (op.type) {
            case OpType::Add: {
                (void)book->add(op.id, op.side, op.price, op.qty, op.ord_type);
                uint64_t cycles = rdtsc() - start;
                add_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
            case OpType::Cancel: {
                (void)book->cancel(op.id);
                uint64_t cycles = rdtsc() - start;
                cancel_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
            case OpType::Match: {
                (void)book->match(op.side, op.qty);
                uint64_t cycles = rdtsc() - start;
                match_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
        }
    }

    uint64_t total_cycles = rdtsc_end() - total_start;
    uint64_t total_ns = cycles_to_ns(total_cycles, freq_ghz);

    // Calculate stats
    auto add_stats = LatencyStats::calc(add_latencies);
    auto cancel_stats = LatencyStats::calc(cancel_latencies);
    auto match_stats = LatencyStats::calc(match_latencies);

    // Print results
    printf("Workload: %zu operations\n", BENCH_OPS);
    printf("  Add:    %zu ops (%.1f%%)\n", add_latencies.size(),
           100.0 * static_cast<double>(add_latencies.size()) / BENCH_OPS);
    printf("  Cancel: %zu ops (%.1f%%)\n", cancel_latencies.size(),
           100.0 * static_cast<double>(cancel_latencies.size()) / BENCH_OPS);
    printf("  Match:  %zu ops (%.1f%%)\n", match_latencies.size(),
           100.0 * static_cast<double>(match_latencies.size()) / BENCH_OPS);

    printf("\nLatency (nanoseconds):\n");
    printf("  Add:    p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           add_stats.p50, add_stats.p90, add_stats.p99, add_stats.p999, add_stats.p9999);
    printf("  Cancel: p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           cancel_stats.p50, cancel_stats.p90, cancel_stats.p99, cancel_stats.p999, cancel_stats.p9999);
    printf("  Match:  p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           match_stats.p50, match_stats.p90, match_stats.p99, match_stats.p999, match_stats.p9999);

    double throughput = static_cast<double>(BENCH_OPS) / (static_cast<double>(total_ns) / 1e9);
    double avg_ns = static_cast<double>(total_ns) / static_cast<double>(BENCH_OPS);

    printf("\nThroughput: %.2f M ops/sec (%.1f ns/op avg)\n",
           throughput / 1e6, avg_ns);

    printf("\nBook state after benchmark:\n");
    printf("  Orders: %zu\n", book->order_count());
    printf("  Pool used: %zu / %zu\n", book->pool_used(), book->pool_capacity());
    if (book->has_bid() && book->has_ask()) {
        printf("  Bid: %ld @ qty %ld\n", book->bid().raw(), book->bid_qty().raw());
        printf("  Ask: %ld @ qty %ld\n", book->ask().raw(), book->ask_qty().raw());
        printf("  Spread: %ld ticks\n", book->spread().raw());
    }

    return 0;
}
