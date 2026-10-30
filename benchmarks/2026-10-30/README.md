# Boggle performance investigation — October 30, 2026

The selected implementation increases measured simulation throughput by
**34–45% with the default compiler target**, and **62–74% when both versions
are compiled for Zen 3**, across the workloads below. It combines an implicit
child trie, neighborhood letter filtering, nearby terminal counters, and a
small table for the first two tiles. Every measured run produced the same
complete frequency table and console ranking as the baseline.

These measurements were made on a **virtualized AMD EPYC 9V74 host**, not the
Ryzen 9 5950X used in the repository's earlier benchmarks. They support testing
this candidate on the 5950X; they do not establish its new workstation speed.

## Baseline and method

- Baseline: `5bb776a9973af6294eb9950e87fd267024e835e3`.
- Compiler: GCC 13.3.0, `-O3 -std=c++17 -pthread`.
- Targeted builds additionally use `-march=znver3` on **both** versions.
- Dictionary: the bundled `words.txt`, 178,548 loaded words at the default
  minimum length of three written letters and three physical tiles.
- Seed: 42. RNG calls, board-index seeding, shuffle order, and face selection
  are identical between versions.
- Trials run in shuffled, interleaved order. Single-worker comparisons use
  CPU affinity 3; four-worker comparisons use CPUs 2–5. Timed experiment
  processes are serialized using a shared lock.
- The times are the program's simulation timer. They exclude dictionary
  loading, worker-counter allocation, final aggregation, and output; they
  include worker startup.
- Every trial checks SHA-256 hashes of **both stdout and the complete CSV**.

Single-worker comparisons use five repetitions. The four-worker comparison
uses three. These are medians, not statistical confidence bounds; host load
still causes variation. Small differences between experimental variants
should not be interpreted as established wins.

### Selected measurements

| Dice / boards | Workers | Compiler target | Baseline median | Candidate median | Throughput increase |
| --- | ---: | --- | ---: | ---: | ---: |
| Boggle83 / 300,000 | 1 | Default | 4.104840 s | 3.066320 s | **33.9%** |
| Boggle83 / 300,000 | 4 | Default | 1.086770 s | 0.751250 s | **44.7%** |
| Big / 100,000 | 1 | Default | 4.090560 s | 2.883390 s | **41.9%** |
| Boggle83 / 300,000 | 1 | `znver3` | 4.250750 s | 2.598630 s | **63.6%** |
| Boggle83 / 300,000 | 4 | `znver3` | 1.115280 s | 0.641062 s | **74.0%** |
| Big / 100,000 | 1 | `znver3` | 3.940940 s | 2.436620 s | **61.7%** |

Throughput increase means `baseline_time / candidate_time - 1`. For example,
33.9% more boards per second corresponds to about 25.3% less simulation time.

For scale, the default single-worker Boggle83 baseline ranged from 3.999740 to
4.624270 seconds, while the selected candidate ranged from 2.912530 to
3.187840 seconds. The improvement is larger than the observed run variation.

The single-worker Boggle83 file also records a simpler late-rank variant:
3.032580 seconds with the default target. Its approximately 1% advantage over
the selected source is too small to distinguish reliably in this environment.
The selected version performs well with both compiler targets and board sizes.

Raw records:

- [4×4, one worker](selected_4x4.json)
- [5×5, one worker](selected_5x5.json)
- [4×4, four workers](selected_4workers.json)
- [Environment, compiler flags, and source/binary hashes](metadata.json)

The 4×4 single-worker trial labels `swar` and `swar_znver3` identify the
selected algorithm before comments and a redundant variable initialization
were cleaned up. Its generic and Zen 3 executables' complete `.text` sections are
byte-identical to those of the cleaned candidate. The 5×5 and four-worker
measurements use the cleaned source directly.

## What the profile showed

A separate instrumented baseline run used 20,000 Boggle83 boards, seed 42,
one worker. It counted operations, rather than using instrumented timings
as performance results. [Raw depth counters](profile.json) show:

- Approximately **493.7 recursive visits per board**.
- Approximately **2,208.8 outgoing-edge checks per board**.
- **74.6%** of edge checks occur at tile depths 1–3.
- **92.5%** occur at tile depths 1–4.
- **80.6%** of edge checks find no matching unused neighboring tile.
- Approximately **28.2%** of terminal visits rediscover a word already found
  on that board.

A separate `gprofng` sampling run attributed roughly 90% of sampled CPU time
to `Worker::visit`. The exact operation counts and sampled time both favor
improving shallow search over optimizing dictionary loading or long paths.
These measurements do not include hardware cache-miss counters; locality is
a design rationale, not a separately measured cache-miss reduction.

## Selected changes

### 1. Remove the edge array

The preceding version already assigned trie nodes in breadth-first order.
That makes a node's children consecutive in memory, in letter order. The
child index therefore needs no stored edge target:

```cpp
child = first_child + popcount(child_mask & ((1u << letter) - 1));
```

A node now stores three 32-bit fields: first child, child-letter mask, and
terminal counter ID. The constructor checks the consecutive-child invariant.
The existing dictionary-size guards remain; there is no new 16-bit offset
limit or fixed-size assumption about the bundled dictionary.

### 2. Filter letters before traversing children

For each board square, preprocessing records which letter types appear in
its neighborhood. At a trie node, the search intersects that mask with the
node's outgoing letters in one operation.

Only surviving letters receive an exact position check. The position mask
still excludes all previously used squares. The preprocessing mask may
include a letter whose only neighboring occurrence is already used, but this
is a conservative overestimate and cannot remove a legal continuation.

The child index is calculated only after an unused matching square exists.
Its rank must be calculated in the **original child mask**, not the filtered
mask; the added tests specifically exercise that distinction.

Fixed 4×4 and 5×5 geometry specializes preprocessing into direct ORs. This
unrolls neighborhood-mask construction, rather than expanding the entire
recursive solver into position-specific routines.

### 3. Group terminal counters by trie depth

The preceding trie was breadth-first, but word counters and stamps were
alphabetically indexed. The selected implementation gives terminal nodes
breadth-first counter IDs. Common short words consequently use nearby
memory locations in each worker.

The alphabetical-to-counter mapping is used once during final aggregation,
so recursive search incurs no mapping lookup. CSV ordering, ranking ties,
word counts, and written-length grouping remain the same.

### 4. Use a small table for the first two tiles

A 26×26 table maps the first two tile letters to their trie node. Search
starts from each ordered adjacent pair, with both squares marked used.
The existing loader accepts only words using at least three physical tiles,
so no terminal word is skipped. Qu remains one tile throughout.

This table is 2,704 bytes. Larger three-letter tables and specialized shuffle
loops were screened separately, but did not establish a worthwhile additional
gain with the preceding packed-edge representation.

### 5. Keep population counts efficient in portable builds

The hot rank calculation uses an inline unsigned 32-bit SWAR population
count. This avoids an out-of-line library helper in the tested generic GCC
build. GCC recognizes the idiom and emits `POPCNT` with `-march=znver3`, as
confirmed in the generated assembly.

The Makefile now accepts `ARCH=znver3` or `ARCH=native`; leaving it unset
retains the compiler's default instruction-set target. Rebuild when changing
the target. Merely adding `-march=znver3` to the baseline did not establish an
improvement; it matters more for the new rank-heavy search.

## Memory accounting

The following are array payload sizes for the bundled dictionary, excluding
the common strings, vector headers, allocator overhead, and root table.

| Array | Previous layout | Selected layout |
| --- | ---: | ---: |
| Trie nodes | 4,738,104 B | 4,738,104 B |
| Explicit packed edges | 1,579,364 B | 0 B |
| Alphabetical-to-counter mapping | 0 B | 714,192 B |
| Two-letter table | 0 B | 2,704 B |
| **Total shown** | **6,317,468 B** | **5,455,000 B** |

The hot node-plus-edge storage falls by 25%. Including the cold mapping and
pair table, these shared indexing arrays fall by approximately 13.6%.
Per-worker 64-bit counters and 32-bit stamps still use 2,142,576 bytes of
array payload per worker for this dictionary.

## Experiments that were not selected

### Smaller nodes and other traversal layouts

The screening included plain bitmap nodes, a mask of letters present anywhere
on the board, an 8-byte representation using incoming child letters, a
separate byte-per-child label array, leaf coalescing, and early terminal
marking. Smaller representations did not consistently beat the selected
neighborhood filter. A separate tightly packed 8-byte bitmap prototype
required restrictive dictionary-size bounds and was unsuitable as a general
default. [Final trie screening](trie-screening.json) records the short trials
for the leading neighbor variants.

### Reverse words and change their orientations

Reversing a sequence of **tiles** preserves its occurrence on every board:
every spelling path can be traversed backward. This is stronger than an
equality of probabilities.

Nevertheless, reversing the entire dictionary increased node count from
394,842 to 510,594 and was substantially slower in initial trials. Choosing
the individually rarer end of each word also grew the trie. Both approaches
lose useful sharing among English prefixes.

A greedy orientation scheme accounting for shared prefixes reduced the
Boggle83 trie to 387,244 nodes, but did not establish a simulation-speed gain
and increased initialization time. There are 444 nonpalindromic tile-reversal
pairs in the dictionary, so sharing their terminal counters remains possible,
but does not automatically remove their prefix searches.

### Exact dice-feasibility pruning

Bipartite matching tests whether each required tile occurrence can be assigned
to a different physical die capable of showing it. This exactly identifies
impossible words for these rectangular boards.

| Dice | Retained words | Removed words | Remaining trie nodes |
| --- | ---: | ---: | ---: |
| Boggle83 | 177,677 | 871 | 392,872 |
| Boggle92 | 168,801 | 9,747 | 371,901 |
| Big | 169,512 | 9,036 | 374,184 |
| Deluxe / Master | 169,535 | 9,013 | 374,235 |

The selected implementation keeps the full dictionary. Many impossible
branches are already unreachable in ordinary search, so pruning them saves
more stored nodes than visited states. The additional matching pass did not
demonstrate a dependable simulation-speed gain. [Exact counts](matching-counts.json)
and [repeated symmetry/pruning screening](symmetry-screening.json) retain
the experimental results.

### Completed-subtree pruning

A worker-local epoch can mark a subtree complete after all its terminal words
have already been discovered on the current board. This is correct and helps
very repetitive boards. However, checking and maintaining these extra marks
throughout normal traversal made the repeated random-board trials roughly
29–33% slower. The selected implementation retains ordinary word stamps.

### Terminal leaves

Encoding a terminal leaf's word ID directly in a packed edge avoids its
recursive call and collapses multiple matching last squares. Short trials
suggested a possible 4×4 benefit, but no 5×5 gain. It was not selected, and
the chosen representation has no explicit edges to tag.

### Compiler and random-generation experiments

For the baseline, a Zen 3 plus LTO/PGO build had a single-worker Boggle83
median of 4.199410 seconds, versus 4.250750 seconds for the ordinary Zen 3
build. This did not establish a useful improvement. Training used different
seeds from benchmarking and included both board sizes.

Compile-time-specialized Fisher–Yates bounds preserve the existing board
stream, but the screened implementation did not show a convincing overall
benefit. [Prefix and shuffle screening](prefix_results.json) records those
trials. Replacing the bounded random mapping with a different unbiased
algorithm would change the seed-to-board correspondence and requires a
separate compatibility decision.

## Symmetry and further mathematical opportunities

### Rotations do not multiply Monte Carlo information

For a square-board rotation or reflection `g`, every word satisfies
`I_w(gB) = I_w(B)`. Thus the eight transformed copies of a random board give
identical frequency observations. Counting them as eight samples does not
improve precision. Symmetry-class caching could help only when independently
generated samples revisit the same orbit often enough to repay its overhead;
that has not been demonstrated here.

Nor can ordinary realized boards restrict search starts to one corner, one
edge, and one interior square: the realized letters usually break those
geometric symmetries. Arbitrary row and column permutations do not preserve
king-move adjacency.

### An exact expected-work objective

For the preceding edge-scanning DFS, let `q(u)` be the probability that a
fixed ordered path spells tile prefix `u`, and let `S(k)` count ordered simple
board paths with `k` cells. By linearity of expectation:

```text
expected recursive calls = sum over nonempty stored prefixes u of S(|u|) q(u)
expected edge scans       = sum over those u of degree(u) S(|u|) q(u)
```

The dice-permanent/generating-function calculation discussed for exact word
path expectations also computes these prefix weights. This supplies a better
objective for jointly choosing word orientations than merely minimizing node
count or a word's initial-letter frequency. It is a proposed next experiment,
not an optimization implemented in this branch. The chosen filtered solver
would need an adjusted cost model for its actual operations.

### Shared computation with a different estimator

One possible future mode samples a dice placement and all faces except one,
then evaluates that remaining die's six faces together. Shared search can
carry a six-bit mask of outcomes for each word, preserving repeated-face
multiplicities. The contribution is the fraction of faces on which the word
occurs. This is a conditional expectation with variance no greater than one
ordinary indicator.

It does **not** follow that this beats six independent boards. If the batch
costs `c` ordinary solves, it improves variance per CPU time only when
`c * Var(conditional contribution) < Var(ordinary indicator)`. It would
change integer-count semantics and belongs in a separate statistical mode.

## Relevant primary sources

- [Vanderkam's jpa-boggle analysis](https://github.com/danvk/jpa-boggle) and
  [boggle-zoo](https://github.com/danvk/boggle-zoo) compare compact tries and
  DAWGs. The contiguous-child/popcount technique has direct precedent there.
  A DAWG needs additional tracking to identify individual words; upstream
  measurements show that a smaller representation need not be faster.
- [hybrid-boggle](https://github.com/danvk/hybrid-boggle) contains generated
  position-specific neighbor traversal. Its maximum-score/orderly-tree
  algorithms address a different task from computing every word's frequency.
  See [the computational-proof paper](https://arxiv.org/abs/2507.02117).
- Daniel Lemire, [Fast Random Integer Generation in an Interval](https://arxiv.org/abs/1805.10941),
  studies unbiased bounded generation with division avoided in the common
  case.
- Brackett-Rozinsky and Lemire,
  [Batched Ranged Random Integer Generation](https://arxiv.org/abs/2408.06213),
  generates several independent bounded integers from one random word and
  applies the method to shuffling. These are useful directions if a future
  RNG-stream version is acceptable.

## Correctness validation

- All **10 regression groups** pass, including the independent Python 4×4
  and 5×5 oracle, Qu, duplicate paths, minimum length, reporting, and worker
  reproducibility.
- Two added groups exercise sparse child-letter bits, filtered rank lookup,
  successive boards with different neighbor masks, counter-ID remapping,
  and forbidden reuse of either tile in the initial pair.
- Every timed comparison verifies the full CSV and console ranking.
- An additional [all-dice comparison](all-dice-validation.json) uses 10,000
  boards per supplied dice file, seed `18446744073709551615`, one baseline
  worker versus four candidate workers, and minimum length four on the
  5×5 sets. All outputs match byte for byte.
- AddressSanitizer and UndefinedBehaviorSanitizer pass all 10 groups.
  LeakSanitizer cannot operate under the traced container environment, so
  that check used `ASAN_OPTIONS=detect_leaks=0`; no leak-check result is claimed.
- The new comparison utility passes a separate Boggle92 smoke comparison
  across one and four workers, recorded in [harness-smoke.json](harness-smoke.json).

## Reproduce on the Ryzen 9 5950X

From a checkout of this branch, build a baseline and the candidate with the
same options. Temporary baseline source/binaries stay outside the checkout:

```sh
git show 5bb776a:boggle.cpp > /tmp/boggle-baseline.cpp
g++ -O3 -std=c++17 -pthread -o /tmp/boggle-baseline /tmp/boggle-baseline.cpp
make clean
make
python3 tests/compare_versions.py /tmp/boggle-baseline ./boggle \
    Boggle83.cub words.txt --sims 1000000 --threads 1,16,32 \
    --repeat 3 --json comparison-generic.json
```

For a comparison targeting Zen 3 on both sides:

```sh
g++ -O3 -std=c++17 -pthread -march=znver3 \
    -o /tmp/boggle-baseline-znver3 /tmp/boggle-baseline.cpp
make clean
make ARCH=znver3
python3 tests/compare_versions.py /tmp/boggle-baseline-znver3 ./boggle \
    Boggle83.cub words.txt --sims 1000000 --threads 1,16,32 \
    --repeat 3 --json comparison-znver3.json
```

Repeat with `Big.cub` and optionally `--min-length 4` for the usual Big Boggle
cutoff. The comparison utility saves every trial, median/range summaries,
binary/input hashes, and output hashes. It stops if any frequency or ranking
differs. These measurements can establish whether the source and instruction
set improvements carry over to the actual workstation and its SMT scaling.
