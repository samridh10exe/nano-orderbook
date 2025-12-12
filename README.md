# Order Book

High-performance limit order book in C++20. 6x faster than std::map baseline.

## Build

```bash
g++ -std=c++20 -O3 -march=native -DNDEBUG -I include -I benchmarks tests/correctness.cpp -o tests
g++ -std=c++20 -O3 -march=native -DNDEBUG -I include -I benchmarks benchmarks/compare.cpp -o compare
./tests && ./compare
```

## Performance

Comparison from `benchmarks/compare.cpp` (1M ops, same workload):

```
             Optimized   std::map    Speedup
Add p50:     16ns        100ns       6.3x
Add p99:     66ns        1148ns      17x
Cancel p50:  111ns       371ns       3.3x
Match p50:   35ns        53ns        1.5x
```

Results vary by CPU, compiler, and environment. Run `compare` for your system.

## Design

Array-indexed price levels (O(1) lookup), 64-byte cache-aligned orders, custom memory pool, sentinel-based intrusive lists. No malloc in hot path.

**Assumptions:** Sequential order IDs, single-threaded, integer tick prices.

## TODO

- Bitmap + TZCNT/LZCNT for O(1) best price updates (eliminate level scanning)
- Better hash function and collision handling for random order IDs
- Pin benchmark to isolated core, serialize rdtsc, measure timer overhead
- Lock-free multi-threaded version with atomic best bid/ask
- Market maker agent with inventory management
