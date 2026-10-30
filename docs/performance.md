# Implementation, benchmarks, and performance investigations

This document holds the technical details and measurements behind the
[Boggle simulator](../README.md). The root README is intended as a concise
introduction and usage guide.

The implementation notes below describe the current CPU solver. Benchmark
figures are **measurements of a particular compiler, dice set, dictionary,
seed, and machine**, not general throughput guarantees. The current Makefile
uses `ARCH=auto` by default: `-march=native` on most systems and GCC 14
with `-march=knl` on Xeon Phi. It rebuilds when these settings change.

## Architecture and performance optimizations

The program retains trie-based depth-first search but reorganizes the
data and computation for fast single-core and multicore execution:

- **Compact shared trie:** The immutable trie is stored in contiguous
  breadth-first order, so frequently visited short prefixes have better
  locality. A node's alphabetically ordered children are consecutive, so a
  **12-byte node** needs only the first child index, a 26-bit child-letter
  mask, and a terminal counter ID. The rank of a letter in the mask identifies
  its child; no separate edge array is needed.
- **Bitmask search:** A 32-bit mask tracks used board positions (up to 25
  tiles). Each board has 26 precomputed masks recording the positions of
  each letter. Each square also has a mask of the letter types present in
  its neighborhood. DFS first intersects that mask with the node's child
  letters, then checks matching unused positions. Child ranks are calculated
  only after an unused matching square is found. Fixed 4×4 and 5×5 geometry
  specializes neighborhood-mask construction into direct bitwise operations.
- **Fast search entry:** A 26×26 lookup table starts searches after the first
  two neighboring tiles. This is valid because every accepted word uses at
  least three physical dice, including words with Qu.
- **Nearby word counters:** Terminal IDs follow breadth-first order, grouping
  the frequently encountered short words together in each worker's counters
  and visit stamps. Final aggregation maps those IDs back to alphabetical
  word order for the existing reports.
- **Independent workers:** Threads share only read-only dice/trie data
  while searching. Each worker owns its word-frequency counters and
  per-board word-visit stamps. There are no shared random generators,
  atomic simulation counters, or locks in the main simulation loop.
- **Deterministic sampling:** Fisher–Yates shuffles the dice; bounded
  rejection sampling selects faces without modulo bias. The RNG stream
  for global board index `i` is initialized with
  `SplitMix64(seed XOR SplitMix64(i))`, not with a thread-specific seed.
  Therefore the same `--seed` and `--sims` produce exactly the same
  board samples and word counts at any thread count. Increasing the
  simulation count preserves the earlier boards as a prefix.
- **Efficient aggregation:** Words are counted once per board using
  private visit stamps; per-worker counts are summed only after all
  simulations finish. The top-N ranking uses a partial sort in each
  length group rather than sorting every word by frequency.

The trie and search optimizations improve both single-thread throughput and
scaling; the independent worker design prevents cross-thread contention.

For the bundled dictionary, the node array occupies **4,738,104 bytes**, versus
**6,317,468 bytes** for the previous nodes and packed edges. The cold counter-ID
mapping and pair table add 714,192 and 2,704 bytes respectively. Thus the hot
trie arrays shrink by 25%, and retained indexing arrays including the added
mapping shrink by approximately 13.6%. Per-worker counter sizes are unchanged.

The [October 2026 performance investigation](../benchmarks/2026-10-30/README.md)
records controlled comparisons with the preceding implementation, rejected
experiments, correctness checks, and relevant sources.

## Ryzen 9 5950X benchmarks

Measured on an **AMD Ryzen 9 5950X** (16 physical cores, 32 logical
threads) using `Boggle83.cub`, **178,548 dictionary words**, **1,000,000
boards**, and `--seed 42`. Both versions were compiled with GCC
`-O3 -std=c++17 -pthread -march=znver3`.

The timings below are medians of **three interleaved trials** at each
thread count, performed on the same workstation. The reference version is
commit `5bb776a` (breadth-first trie with packed edges); the current
implementation adds implicit children, neighborhood-letter filtering, a
two-tile prefix table, and breadth-first counter placement.

| Threads | Previous simulation time | Current simulation time | Current boards/second | Throughput improvement |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 8.958310 s | **5.639310 s** | **177,327** | **+58.9%** |
| 16 | 0.656888 s | **0.415791 s** | **2,405,055** | **+58.0%** |
| 32 | 0.443750 s | **0.266992 s** | **3,745,430** | **+66.2%** |

Relative to one thread, the current version scales to about **13.56× at
16 threads** (84.8% parallel efficiency) and **21.12× at 32 threads**.
Simultaneous multithreading raises throughput by about **55.7%** from
16 to 32 threads. With all 32 logical threads, the simulator evaluates
approximately **3.75 million boards per second**.

All rankings and complete `word,frequency` CSV outputs were **byte-identical
across the two implementations and all tested thread counts**, and all
10 regression tests passed on the 5950X. These results refer to the
**simulation stage** only, excluding dictionary construction, thread
setup outside the measured region, and output. CPU load and compiler
version can affect results.

For reproducibility, see the
[October 2026 optimization investigation](../benchmarks/2026-10-30/README.md)
and the `tests/compare_versions.py` utility below. The preceding layout's
earlier 1/2/4/8/16/32-worker measurements used generic `-O3` builds,
so they should not be compared directly against the Zen 3-specific
measurements in this table.

## Performance reproduction and correctness

```sh
make test
python3 tests/benchmark.py ./boggle Boggle83.cub words.txt 50000 42
```

The test suite includes an independent Python reference implementation,
4×4 and 5×5 boards, Qu handling, duplicate word paths, different thread
counts, input validation, top-N sorting and CSV output. The benchmark
script checks that both the ranking and full CSV frequencies are identical
across the tested worker counts.

To compare two compiled versions, interleave repeated runs and check their
complete CSVs as well as console rankings:

```sh
python3 tests/compare_versions.py /path/to/baseline ./boggle \
    Boggle83.cub words.txt --sims 1000000 --threads 1,16,32 \
    --repeat 3 --json comparison.json
```

This reports median simulation times and throughput ratios, and saves raw
timings plus input/output hashes when `--json` is supplied. Use the same
compiler flags for both versions to isolate the source changes.

For additional memory-safety testing (GCC or Clang):

```sh
g++ -std=c++17 -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -pthread -o boggle_san boggle.cpp
python3 tests/test_boggle.py ./boggle_san
```


## Experiment archive

The [October 30, 2026 investigation](../benchmarks/2026-10-30/README.md)
contains the raw benchmark records, profiling, failed alternatives, output
hash comparisons, and references to the earlier implementations. The full
results are retained there rather than expanding the main README with
experimental history.
