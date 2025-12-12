#include "types.hpp"
#include "timer.hpp"
#include "workload.hpp"
#include <cstdio>
#include <map>
#include <list>
#include <unordered_map>

using namespace ob;

// Naive order book implementation using std::map + std::list
class NaiveOrderBook {
    struct NaiveOrder {
        OrderId id;
        Price price;
        Qty qty;
        Side side;
    };

    // std::map for price levels, std::list for orders at each level
    std::map<int64_t, std::list<NaiveOrder>> bids_;  // Descending
    std::map<int64_t, std::list<NaiveOrder>> asks_;  // Ascending

    // Order lookup
    std::unordered_map<uint64_t, std::pair<int64_t, std::list<NaiveOrder>::iterator>> order_map_;

public:
    bool add(OrderId id, Side side, Price px, Qty qty) {
        if (order_map_.count(id.raw())) return false;

        NaiveOrder o{id, px, qty, side};

        if (side == Side::Buy) {
            auto& level = bids_[px.raw()];
            level.push_back(o);
            order_map_[id.raw()] = {px.raw(), std::prev(level.end())};
        } else {
            auto& level = asks_[px.raw()];
            level.push_back(o);
            order_map_[id.raw()] = {px.raw(), std::prev(level.end())};
        }
        return true;
    }

    bool cancel(OrderId id) {
        auto it = order_map_.find(id.raw());
        if (it == order_map_.end()) return false;

        int64_t px = it->second.first;
        auto list_it = it->second.second;
        Side side = list_it->side;

        if (side == Side::Buy) {
            auto& level = bids_[px];
            level.erase(list_it);
            if (level.empty()) bids_.erase(px);
        } else {
            auto& level = asks_[px];
            level.erase(list_it);
            if (level.empty()) asks_.erase(px);
        }

        order_map_.erase(it);
        return true;
    }

    Qty match(Side aggressor, Qty qty) {
        if (aggressor == Side::Buy) {
            // Match against asks (ascending price)
            while (qty.raw() > 0 && !asks_.empty()) {
                auto it = asks_.begin();
                auto& level = it->second;

                while (qty.raw() > 0 && !level.empty()) {
                    auto& front = level.front();
                    Qty fill = std::min(qty, front.qty);
                    front.qty -= fill;
                    qty -= fill;

                    if (front.qty.raw() <= 0) {
                        order_map_.erase(front.id.raw());
                        level.pop_front();
                    }
                }

                if (level.empty()) {
                    asks_.erase(it);
                }
            }
        } else {
            // Match against bids (descending price)
            while (qty.raw() > 0 && !bids_.empty()) {
                auto it = bids_.rbegin();
                int64_t px = it->first;
                auto& level = bids_[px];

                while (qty.raw() > 0 && !level.empty()) {
                    auto& front = level.front();
                    Qty fill = std::min(qty, front.qty);
                    front.qty -= fill;
                    qty -= fill;

                    if (front.qty.raw() <= 0) {
                        order_map_.erase(front.id.raw());
                        level.pop_front();
                    }
                }

                if (level.empty()) {
                    bids_.erase(px);
                }
            }
        }
        return qty;
    }

    bool has_bid() const { return !bids_.empty(); }
    bool has_ask() const { return !asks_.empty(); }

    Price bid() const {
        return bids_.empty() ? Price{-1} : Price{bids_.rbegin()->first};
    }

    Price ask() const {
        return asks_.empty() ? Price{DEFAULT_MAX_PRICE + 1} : Price{asks_.begin()->first};
    }
};

static constexpr size_t WARMUP_OPS = 10000;
static constexpr size_t BENCH_OPS = 1'000'000;  // Fewer ops since baseline is slower

int main() {
    printf("=== Baseline (std::map) Benchmark ===\n\n");

    double freq_ghz = get_cpu_freq_ghz();
    printf("CPU frequency: %.2f GHz\n", freq_ghz);

    NaiveOrderBook book;

    printf("Generating %zu operations...\n", WARMUP_OPS + BENCH_OPS);
    WorkloadGen gen(42);
    auto warmup_ops = gen.generate(WARMUP_OPS);
    auto bench_ops = gen.generate(BENCH_OPS);

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
                book.add(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel:
                book.cancel(op.id);
                break;
            case OpType::Match:
                book.match(op.side, op.qty);
                break;
        }
    }

    // Reset
    book = NaiveOrderBook{};
    gen.reset(42);
    (void)gen.generate(WARMUP_OPS);

    // Benchmark
    printf("Running benchmark (%zu ops)...\n\n", BENCH_OPS);

    uint64_t total_start = rdtsc_start();

    for (const auto& op : bench_ops) {
        uint64_t start = rdtsc();

        switch (op.type) {
            case OpType::Add: {
                book.add(op.id, op.side, op.price, op.qty);
                uint64_t cycles = rdtsc() - start;
                add_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
            case OpType::Cancel: {
                book.cancel(op.id);
                uint64_t cycles = rdtsc() - start;
                cancel_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
            case OpType::Match: {
                book.match(op.side, op.qty);
                uint64_t cycles = rdtsc() - start;
                match_latencies.push_back(cycles_to_ns(cycles, freq_ghz));
                break;
            }
        }
    }

    uint64_t total_cycles = rdtsc_end() - total_start;
    uint64_t total_ns = cycles_to_ns(total_cycles, freq_ghz);

    auto add_stats = LatencyStats::calc(add_latencies);
    auto cancel_stats = LatencyStats::calc(cancel_latencies);
    auto match_stats = LatencyStats::calc(match_latencies);

    printf("Workload: %zu operations\n", BENCH_OPS);
    printf("  Add:    %zu ops (%.1f%%)\n", add_latencies.size(),
           100.0 * add_latencies.size() / BENCH_OPS);
    printf("  Cancel: %zu ops (%.1f%%)\n", cancel_latencies.size(),
           100.0 * cancel_latencies.size() / BENCH_OPS);
    printf("  Match:  %zu ops (%.1f%%)\n", match_latencies.size(),
           100.0 * match_latencies.size() / BENCH_OPS);

    printf("\nLatency (nanoseconds):\n");
    printf("  Add:    p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           add_stats.p50, add_stats.p90, add_stats.p99, add_stats.p999, add_stats.p9999);
    printf("  Cancel: p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           cancel_stats.p50, cancel_stats.p90, cancel_stats.p99, cancel_stats.p999, cancel_stats.p9999);
    printf("  Match:  p50=%-4lu p90=%-4lu p99=%-4lu p99.9=%-4lu p99.99=%-4lu\n",
           match_stats.p50, match_stats.p90, match_stats.p99, match_stats.p999, match_stats.p9999);

    double throughput = BENCH_OPS / (static_cast<double>(total_ns) / 1e9);
    double avg_ns = static_cast<double>(total_ns) / BENCH_OPS;

    printf("\nThroughput: %.2f M ops/sec (%.1f ns/op avg)\n",
           throughput / 1e6, avg_ns);

    return 0;
}
