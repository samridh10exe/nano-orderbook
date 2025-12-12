#include "order_book.hpp"
#include "timer.hpp"
#include "workload.hpp"
#include <cstdio>
#include <vector>
#include <map>
#include <list>
#include <unordered_map>
#include <memory>

using namespace ob;

// Naive baseline
class NaiveBook {
    struct NaiveOrder { OrderId id; Price price; Qty qty; Side side; };
    std::map<int64_t, std::list<NaiveOrder>> bids_, asks_;
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
            while (qty.raw() > 0 && !asks_.empty()) {
                auto it = asks_.begin();
                auto& level = it->second;
                while (qty.raw() > 0 && !level.empty()) {
                    auto& front = level.front();
                    Qty fill = std::min(qty, front.qty);
                    front.qty -= fill; qty -= fill;
                    if (front.qty.raw() <= 0) { order_map_.erase(front.id.raw()); level.pop_front(); }
                }
                if (level.empty()) asks_.erase(it);
            }
        } else {
            while (qty.raw() > 0 && !bids_.empty()) {
                auto it = bids_.rbegin();
                int64_t px = it->first;
                auto& level = bids_[px];
                while (qty.raw() > 0 && !level.empty()) {
                    auto& front = level.front();
                    Qty fill = std::min(qty, front.qty);
                    front.qty -= fill; qty -= fill;
                    if (front.qty.raw() <= 0) { order_map_.erase(front.id.raw()); level.pop_front(); }
                }
                if (level.empty()) bids_.erase(px);
            }
        }
        return qty;
    }
};

static constexpr size_t OPS = 1'000'000;

template<typename Book>
void bench(const char* name, std::vector<Op>& ops, double freq) {
    auto book = std::make_unique<Book>();

    std::vector<uint64_t> add_lat, cancel_lat, match_lat;
    add_lat.reserve(OPS); cancel_lat.reserve(OPS); match_lat.reserve(OPS);

    for (const auto& op : ops) {
        uint64_t t0 = rdtsc();
        switch (op.type) {
            case OpType::Add:
                (void)book->add(op.id, op.side, op.price, op.qty);
                add_lat.push_back(rdtsc() - t0);
                break;
            case OpType::Cancel:
                (void)book->cancel(op.id);
                cancel_lat.push_back(rdtsc() - t0);
                break;
            case OpType::Match:
                (void)book->match(op.side, op.qty);
                match_lat.push_back(rdtsc() - t0);
                break;
        }
    }

    auto to_ns = [freq](std::vector<uint64_t>& v) -> LatencyStats {
        for (auto& c : v) c = cycles_to_ns(c, freq);
        return LatencyStats::calc(v);
    };

    auto add_s = to_ns(add_lat);
    auto cancel_s = to_ns(cancel_lat);
    auto match_s = to_ns(match_lat);

    printf("\n%s:\n", name);
    printf("  Add:    p50=%-4lu p90=%-4lu p99=%-4lu (n=%zu)\n",
           add_s.p50, add_s.p90, add_s.p99, add_lat.size());
    printf("  Cancel: p50=%-4lu p90=%-4lu p99=%-4lu (n=%zu)\n",
           cancel_s.p50, cancel_s.p90, cancel_s.p99, cancel_lat.size());
    printf("  Match:  p50=%-4lu p90=%-4lu p99=%-4lu (n=%zu)\n",
           match_s.p50, match_s.p90, match_s.p99, match_lat.size());
}

int main() {
    printf("=== Order Book Comparison (same workload) ===\n");

    double freq = get_cpu_freq_ghz();
    printf("CPU: %.2f GHz, Ops: %zu\n", freq, OPS);

    // Generate identical workload
    WorkloadGen gen(42, 1000.0, 50000, 50.0, 0.35, 0.25, 0.05);
    auto ops = gen.generate(OPS);

    printf("\nWorkload mix:");
    size_t a=0, c=0, m=0;
    for (auto& op : ops) {
        if (op.type == OpType::Add) ++a;
        else if (op.type == OpType::Cancel) ++c;
        else ++m;
    }
    printf(" Add=%zu (%.0f%%), Cancel=%zu (%.0f%%), Match=%zu (%.0f%%)\n",
           a, 100.0*a/OPS, c, 100.0*c/OPS, m, 100.0*m/OPS);

    // Benchmark optimized
    using OptBook = OrderBook<100000, 500000>;
    bench<OptBook>("Optimized (array + pool)", ops, freq);

    // Benchmark baseline
    bench<NaiveBook>("Baseline (std::map)", ops, freq);

    printf("\n");
    return 0;
}
