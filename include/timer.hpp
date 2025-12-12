#pragma once

#include <cstdint>
#include <algorithm>
#include <vector>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>

namespace ob {

// rdtsc for cycle-accurate timing
inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// rdtscp with serialization (more accurate for benchmarks)
inline uint64_t rdtscp() noexcept {
    uint32_t lo, hi, aux;
    __asm__ volatile(
        "rdtscp"
        : "=a"(lo), "=d"(hi), "=c"(aux)
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// full fence + rdtsc for most accurate start timing
inline uint64_t rdtsc_start() noexcept {
    uint32_t lo, hi;
    __asm__ volatile(
        "cpuid\n\t"
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        :: "rbx", "rcx"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// rdtscp + fence for most accurate end timing
inline uint64_t rdtsc_end() noexcept {
    uint32_t lo, hi;
    __asm__ volatile(
        "rdtscp\n\t"
        "lfence"
        : "=a"(lo), "=d"(hi)
        :: "rcx"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

// get cpu frequency from /proc/cpuinfo (linux)
inline double get_cpu_freq_ghz() noexcept {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("cpu MHz") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                double mhz = std::stod(line.substr(pos + 1));
                return mhz / 1000.0;
            }
        }
    }
    // fallback: calibrate using steady_clock
    auto t1 = std::chrono::steady_clock::now();
    uint64_t c1 = rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint64_t c2 = rdtsc();
    auto t2 = std::chrono::steady_clock::now();

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    return static_cast<double>(c2 - c1) / static_cast<double>(ns);
}

// convert cycles to nanoseconds
inline uint64_t cycles_to_ns(uint64_t cycles, double freq_ghz) noexcept {
    return static_cast<uint64_t>(static_cast<double>(cycles) / freq_ghz);
}

// percentile calculation (modifies vector by sorting)
inline uint64_t percentile(std::vector<uint64_t>& data, double p) {
    if (data.empty()) return 0;
    std::sort(data.begin(), data.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(data.size() - 1));
    return data[idx];
}

// percentile on pre-sorted data
inline uint64_t percentile_sorted(const std::vector<uint64_t>& data, double p) {
    if (data.empty()) return 0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(data.size() - 1));
    return data[idx];
}

// latency statistics
struct LatencyStats {
    uint64_t p50;
    uint64_t p90;
    uint64_t p99;
    uint64_t p999;
    uint64_t p9999;
    uint64_t min;
    uint64_t max;
    double avg;

    static LatencyStats calc(std::vector<uint64_t>& data) {
        if (data.empty()) return {};

        std::sort(data.begin(), data.end());

        uint64_t sum = 0;
        for (auto v : data) sum += v;

        return {
            .p50   = percentile_sorted(data, 0.50),
            .p90   = percentile_sorted(data, 0.90),
            .p99   = percentile_sorted(data, 0.99),
            .p999  = percentile_sorted(data, 0.999),
            .p9999 = percentile_sorted(data, 0.9999),
            .min   = data.front(),
            .max   = data.back(),
            .avg   = static_cast<double>(sum) / static_cast<double>(data.size())
        };
    }
};

// scoped timer
class ScopedTimer {
    uint64_t start_;
    uint64_t& dest_;
public:
    explicit ScopedTimer(uint64_t& dest) noexcept
        : start_(rdtsc_start()), dest_(dest) {}

    ~ScopedTimer() noexcept {
        dest_ = rdtsc_end() - start_;
    }
};

} // namespace ob
