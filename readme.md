# `mprobe`

A zero-copy, SPMC   memory-mapped shared hash map engineered in C++20 for sub-microsecond IPC and market data telemetry. Backed by POSIX shared memory (`/dev/shm`), huge pages, and manual L1 cache line prefetching.

---

## Architecture & Mechanical Sympathy

NOTE : The hash map are good for read heavy worklads if there is a write heavy workload I recommned using a producer subscriber lock free architecture to aviod the coherecy issues and RFO invalidation signals that plagues multiple cores fighting to write and read a single memory location. Recommended Reader Writer ratio 100+ : 1 . The hash map can scale on multiple cores well so far the ratio is resepcted. The writer can write 10,000 writes to 100,000 writes per second and the presure is negligable because of how much the readers access is randomized and the amount of reads outway writes greatly. 
```
                  Writer Process (Feed Handler / ITCH)
                               │
            ┌──────────────────┴──────────────────┐
            │  atomic stores (release semantics)   │
            ▼                                     ▼
┌───────────────────────────────────────────────────────────────┐
│               POSIX Shared Memory (/dev/shm)                  │
│                     MAP_SHARED + Hugepages                    │
│                                                               │
│  ┌───────────────────────┐         ┌───────────────────────┐  │
│  │ Slot 0 (64B Aligned)  │  ...    │ Slot N (64B Aligned)  │  │
│  │ key | seq | payload   │         │ key | seq | payload   │  │
│  └───────────────────────┘         └───────────────────────┘  │
└───────────────────────────────────────────────────────────────┘
            ▲                                     ▲
            │   atomic loads (acquire semantics)  │
            └──────────────────┬──────────────────┘
                               │
            Reader Process(es) (OMD, Alpha, Risk)
         [__builtin_prefetch pipeline + zero bus locks]
```

Modern DRAM access costs $\sim 50\text{--}100\text{ ns}$. Under sub-microsecond budgets ($< 1\,\mu\text{s}$ at $p99.9$), a single cache miss or TLB page walk invalidates latency SLAs. `mprobe` aligns data layouts to CPU silicon architecture to keep lookups inside L1/L2 caches.

### Key Engineering Invariants
* **64-Byte Cache Line Slot Alignment (`alignas(64)`):** Every bucket matches the hardware cache line boundary. Modifying slot $i$ never invalidates slot $i+1$ on an adjacent core's L1 cache, eliminating false sharing.
* **Prefetch Pipeline (`__builtin_prefetch`):** Open-addressing collision loops issue asynchronous prefetch instructions (`PREFETCHT0`) on the next predicted probe index before comparing keys on the current slot. Memory stalls overlap with ALU comparison instructions.
* **Cross-Process Absolute Pointer Elimination:** Virtual base addresses differ across processes. Storage relies strictly on power-of-two index arithmetic and relative offsets; raw virtual pointers never touch the shared memory segment.
* **Deterministic Initialization:** Bypasses demand paging using `mmap(..., MAP_SHARED | MAP_POPULATE)`. Allocates backed pages ahead of time to eliminate kernel page faults during trading loops.
* **Wait-Free Readers:** Readers execute exclusively via `std::memory_order_acquire` loads and sequence verification. Zero reader locks, zero atomic read-modify-write operations (`LOCK CMPXCHG`), and zero cross-core cache invalidation traffic.

---

## Memory Layout

The underlying backing file maintains a cache-aligned header followed by a contiguous array of $2^N$ slots:

```
0x0000 ┌────────────────────────────────────────────────────────┐
       │ Header: Magic, Version, Capacity, Slot Mask (64 Bytes) │
0x0040 ├────────────────────────────────────────────────────────┤
       │ Slot 0: Key (8B) | Seq (8B) | Val (32B) | Pad (16B)    │
0x0080 ├────────────────────────────────────────────────────────┤
       │ Slot 1: Key (8B) | Seq (8B) | Val (32B) | Pad (16B)    │
       │ ...                                                    │
End    └────────────────────────────────────────────────────────┘
```

---

## Latency Profile

Benchmarked on an isolated core running pinned threads via `taskset -c` under Linux kernel 6.x (`rdtsc` cycles converted to nanoseconds):

| Operation | Metric | Latency (ns) | CPU Instructions | L1-dcache Misses |
| :--- | :--- | :--- | :--- | :--- |
| **Lookup (L1 Hit)** | $p50$ | `18 ns` | ~24 | 0 |
| **Lookup (Collision + Prefetch)** | $p99$ | `42 ns` | ~58 | 0 (Prefetched) |
| **Lookup (Cold Line)** | $p99.9$ | `88 ns` | ~60 | 1 |
| **Writer Insert / Update** | $p50$ | `28 ns` | ~35 | 0 |

---

## Directory Structure

```
mprobe/
├── CMakeLists.txt
├── include/
│   └── mprobe/
│       ├── hash.hpp       # Branchless Murmur/wyhash64 power-of-two hasher
│       ├── layout.hpp     # 64-byte aligned structs & header specifications
│       ├── map.hpp        # Core probe loop & __builtin_prefetch logic
│       └── shm.hpp        # POSIX shm_open / ftruncate / mmap lifecycle wrapper
├── bench/
│   └── latency_bench.cpp  # rdtsc-based microbenchmarks with p50/p99/p99.9 breakdown
├── src/
│   ├── reader_node.cpp    # Standalone reader process harness
│   └── writer_node.cpp    # Standalone feed producer harness
└── tests/
    └── concurrency_test.cpp
```

---

## Quickstart

### Prerequisites
* Clang 17+ or GCC 13+ with `-std=c++20`
* Linux kernel with `/dev/shm` enabled
* Root / Capabilities for Huge Pages (optional, recommended)

### Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
```

### Running Producer / Consumer

**Terminal 1 (Writer / Ingest):**
```bash
# Pin writer to isolated core 2
taskset -c 2 ./src/writer_node --shm /mprobe_orderbook --slots 1048576
```

**Terminal 2 (Reader / Alpha Strategy):**
```bash
# Pin reader to isolated core 3
taskset -c 3 ./src/reader_node --shm /mprobe_orderbook
```

---

## Performance Tuning Checklist

To reproduce sub-microsecond tail bounds:

1. **Mount Huge Pages:**
   ```bash
   sudo sysctl -w vm.nr_hugepages=1024
   ```
2. **Disable Frequency Scaling:**
   ```bash
   sudo cpupower frequency-set --governor performance
   ```
3. **Isolate Cores via GRUB:**
   Append `isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3` to `/etc/default/grub` to isolate benchmark threads from OS scheduler ticks.
4. **Inspect Cache Misses with `perf`:**
   ```bash
   perf stat -e L1-dcache-loads,L1-dcache-load-misses,dTLB-load-misses ./bench/latency_bench
   ```

---

## License

MIT
