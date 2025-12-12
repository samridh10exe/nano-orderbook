#pragma once

#include "types.hpp"
#include <random>
#include <cmath>
#include <vector>
#include <algorithm>

namespace ob {

enum class OpType : uint8_t {
    Add = 0,
    Cancel = 1,
    Match = 2
};

struct Op {
    OpType type;
    OrderId id;
    Side side;
    Price price;
    Qty qty;
    OrdType ord_type;
};

// Realistic workload generator
class WorkloadGen {
    std::mt19937_64 rng_;

    // Distributions
    std::exponential_distribution<double> poisson_;
    std::normal_distribution<double> price_dist_;
    std::uniform_real_distribution<double> uniform_;

    // Config
    double cancel_rate_;
    double market_rate_;
    double ioc_rate_;
    double pareto_alpha_;
    int64_t mid_price_;
    int64_t max_price_;

    uint64_t next_id_ = 1;
    std::vector<OrderId> active_ids_;

public:
    explicit WorkloadGen(
        uint64_t seed = 12345,
        double lambda = 1000.0,       // Orders per "unit time"
        int64_t mid = 50000,          // Mid-market price
        double price_std = 100.0,     // Price stddev in ticks
        double cancel_rate = 0.40,    // 40% cancels
        double market_rate = 0.30,    // 30% market orders
        double ioc_rate = 0.10,       // 10% IOC
        double pareto_alpha = 1.5,    // Size distribution
        int64_t max_price = DEFAULT_MAX_PRICE
    )
        : rng_(seed)
        , poisson_(lambda)
        , price_dist_(static_cast<double>(mid), price_std)
        , uniform_(0.0, 1.0)
        , cancel_rate_(cancel_rate)
        , market_rate_(market_rate)
        , ioc_rate_(ioc_rate)
        , pareto_alpha_(pareto_alpha)
        , mid_price_(mid)
        , max_price_(max_price)
    {}

    Op next() {
        double r = uniform_(rng_);

        // Decide operation type
        if (r < cancel_rate_ && !active_ids_.empty()) {
            return gen_cancel();
        }

        r = uniform_(rng_);
        if (r < market_rate_) {
            return gen_market();
        }

        return gen_limit();
    }

    // Generate n operations
    std::vector<Op> generate(size_t n) {
        std::vector<Op> ops;
        ops.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            ops.push_back(next());
        }
        return ops;
    }

    // Reset state
    void reset(uint64_t seed) {
        rng_.seed(seed);
        next_id_ = 1;
        active_ids_.clear();
    }

private:
    Op gen_limit() {
        Op op;
        op.type = OpType::Add;
        op.id = OrderId{next_id_++};
        op.side = uniform_(rng_) < 0.5 ? Side::Buy : Side::Sell;
        op.price = gen_price(op.side);
        op.qty = gen_qty();

        // Decide limit vs IOC
        double r = uniform_(rng_);
        if (r < ioc_rate_) {
            op.ord_type = OrdType::IOC;
        } else {
            op.ord_type = OrdType::Limit;
            active_ids_.push_back(op.id);
        }

        return op;
    }

    Op gen_market() {
        Op op;
        op.type = OpType::Match;
        op.id = OrderId{0};
        op.side = uniform_(rng_) < 0.5 ? Side::Buy : Side::Sell;
        op.price = Price{0};
        op.qty = gen_qty();
        op.ord_type = OrdType::Market;
        return op;
    }

    Op gen_cancel() {
        // Pick random active order
        std::uniform_int_distribution<size_t> idx_dist(0, active_ids_.size() - 1);
        size_t idx = idx_dist(rng_);

        Op op;
        op.type = OpType::Cancel;
        op.id = active_ids_[idx];
        op.side = Side::Buy;  // unused
        op.price = Price{0};  // unused
        op.qty = Qty{0};      // unused
        op.ord_type = OrdType::Limit;

        // Remove from active list (swap and pop)
        active_ids_[idx] = active_ids_.back();
        active_ids_.pop_back();

        return op;
    }

    Price gen_price(Side side) {
        // Normal distribution around mid
        double px = price_dist_(rng_);

        // Bias: bids below mid, asks above mid
        if (side == Side::Buy) {
            px = std::min(px, static_cast<double>(mid_price_) - 1);
        } else {
            px = std::max(px, static_cast<double>(mid_price_) + 1);
        }

        // Clamp to valid range
        int64_t ipx = std::clamp(
            static_cast<int64_t>(px),
            int64_t{0},
            max_price_
        );

        return Price{ipx};
    }

    Qty gen_qty() {
        // Pareto distribution (heavy-tailed)
        // P(X > x) = (x_min / x)^alpha
        double u = uniform_(rng_);
        double x_min = 1.0;
        double qty = x_min / std::pow(u, 1.0 / pareto_alpha_);

        // Clamp to reasonable range
        int64_t iqty = std::clamp(
            static_cast<int64_t>(qty),
            int64_t{1},
            int64_t{10000}
        );

        return Qty{iqty};
    }
};

} // namespace ob
