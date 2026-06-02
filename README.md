# nano-orderbook

`nano-orderbook` is a C++20 single-threaded limit order book that keeps price levels, order lookup, and order allocation on predictable memory paths for low-latency add, cancel, and match operations.

## Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Usage

```bash
$ ./build/tests
=== Order Book Correctness Tests ===
...
=== All tests passed ===
```

```bash
$ ./build/compare
=== Order Book Comparison (same workload) ===
...
Optimized (array + pool):
  Add:    p50=...
Baseline (std::map):
  Add:    p50=...
```

## How It Works

The book indexes integer price levels in an array, stores orders in a fixed pool, and links orders through intrusive per-level queues. The benchmark uses `rdtsc` on x86 and `steady_clock` elsewhere, so compare numbers only within the same machine, compiler, and workload.

SPDX-License-Identifier: MIT
