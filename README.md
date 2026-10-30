# Boggle word-frequency simulator

A corrected, parallel C++17 implementation of `octonion/boggle`'s Monte Carlo
simulator. Given a physical Boggle dice set and a word list, it estimates how
often each dictionary word appears on a randomly shaken board. Each word counts
**once per board**, even if it can be spelled by several paths.

## Build and run

Requires a C++17 compiler and standard POSIX thread support; no Boost dependency.

```sh
make
./boggle Boggle83.cub words.txt --sims 100000 --threads 4 --seed 1 --top 10
./boggle Boggle83.cub words.txt --sims 100000 --threads 4 --seed 1 --top 20 --csv frequencies.csv
```

The original two-argument invocation still works, with defaults of 100,000
simulations, one worker, seed 1, and **10 results per word length**:

```sh
./boggle Boggle83.cub words.txt
```

Standard output groups words by *written spelling length*, shortest to
longest. Thus `qu` contributes two characters to the word's length even though
it occupies one Boggle tile. Within each length, words are ranked by descending
number of boards on which they appear; ties are sorted alphabetically. A length
with fewer than `--top N` discovered words prints all discovered words of that
length. Lengths with no discovered words are omitted.

Each ranking row shows the word, integer occurrence count, and estimated
probability (percentage of all simulated boards). With `--sims 0`, the ranking
reports that no words were found.

Use **`--csv frequencies.csv`** to additionally write the complete sorted
`word,frequency` table to disk. It starts with the header `word,frequency`;
`frequency` is the **integer number of boards containing the word**, not a
normalized probability. Only nonzero frequencies are included, as in the
original version's stdout output. Standard output continues to show the
rankings, even when a CSV file is requested. Build and simulation timings are
written to stderr.

Use `./boggle --help` to see all options. The source also builds directly with:

```sh
g++ -std=c++17 -O3 -pthread -Wall -Wextra -o boggle boggle.cpp
```

## Dice and dictionary

* Dice files contain one lowercase six-letter string per die (six equiprobable
  faces). Exactly 16 or 25 dice are supported, corresponding to 4x4 and 5x5
  boards. The original repository contains `Boggle83.cub`, `Boggle92.cub`,
  `Big.cub`, `Deluxe.cub` and `Master.cub`.
* Word lists have one whitespace-delimited word per entry. Case is ignored,
  duplicate words are removed, and entries containing nonletters are ignored.
* A `q` cube represents **Qu**. Words containing a `q` not followed by `u`
  are excluded. As in the original simulator, words must occupy **at least
  three dice** (not merely three written characters): for example `qua`
  occupies two dice and is excluded.

## Correctness and concurrency

* The dictionary is an immutable, compact trie shared by all workers. Both
  terminal words and longer words that extend them can be recognized.
* The trie is laid out in breadth-first order to improve prefix locality.
  Child indices and labels are packed into 32-bit edges instead of 8-byte
  edge records, reducing cache traffic without changing search semantics.
* Each worker maintains private visited-word stamps and private counts; no
  dictionary mutations or shared hot counters occur during simulation.
* The board visit set is a 32-bit local mask; recursive searches neither
  reuse a cell nor modify shared search state.
* For each board, a 26-entry array of letter-position masks lets the search
  iterate actual trie edges and adjacent matching tiles instead of probing
  every neighbor for a (typically absent) trie child. This improves both
  single-thread and multi-thread throughput without changing output.
* Dice are uniformly shuffled with Fisher-Yates and each face is rolled
  uniformly using rejection sampling. A PCG-style generator gets a unique
  SplitMix64-derived stream for each **board index**, not each worker.
* The same `--seed` and `--sims` give identical output regardless of thread
  count or static work partition. This also makes single- and multi-threaded
  verification straightforward.
* The number of simulations is divided deterministically among workers;
  there is no global atomic counter or scheduling lock on the hot path.

## Tests

```sh
make test
```

Tests include an independent Python reference Boggle solver, comparison of
exact word counts across different thread counts and board sizes, `Qu`,
prefix/longer words, duplicate paths, malformed dice, empty dictionaries,
per-length top-N ranking and alphabetical ties, optional CSV output and errors.
The test suite verifies complete frequencies through the CSV file, not merely
the truncated ranking output.

For sanitizers (GCC or Clang):

```sh
g++ -std=c++17 -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -pthread -o boggle_san boggle.cpp
python3 tests/test_boggle.py ./boggle_san
```

Optionally benchmark on your own machine (uses existing dice and dictionary):

```sh
python3 tests/benchmark.py ./boggle Boggle83.cub words.txt 50000
```

The benchmark checks that all thread counts produce identical ranked output
and complete CSV frequency tables. For
speedups, use enough boards that initialization and thread-startup overhead
are negligible. A 16-core machine is best characterized by 1, 2, 4, 8 and
16 workers. SMT scaling past 16 workers is an additional experiment.

## Single-core performance

The board-search optimization in this revision reduces failed trie lookups.
On a four-CPU quota environment with a 117k-word dictionary and 100,000
simulations, median end-to-end runtime dropped from about 3.47 seconds to
2.04 seconds versus the preceding corrected version; the legacy-style
baseline ran in about 3.61 seconds. These are hardware-specific measurements,
not a guarantee for other machines. All tested thread counts produce
bit-identical output for the fixed seed.

## Ryzen 9 5950X performance (October 2026)

Measured with `Boggle83.cub`, a 178,548-word dictionary, 1,000,000 boards,
seed 42, and GCC `-O3`. These timings are simulation-only, not end-to-end.

| Workers | Before BFS/packing | After BFS/packing | Throughput gain |
| ---: | ---: | ---: | ---: |
| 1 | 10.4272 s | 8.9242 s | 16.8% |
| 2 | 5.3006 s | 4.4518 s | 19.1% |
| 4 | 2.7669 s | 2.3458 s | 18.0% |
| 8 | 1.4628 s | 1.2195 s | 19.9% |
| 16 | 0.7704 s | 0.6347 s | 21.4% |
| 32 | 0.5196 s | 0.4347 s | 19.5% |

The optimized implementation reached approximately 2.30 million boards/s
with 32 workers and 1.58 million boards/s with 16 workers, showing strong SMT
scaling. Loading the BFS/packed trie took approximately 0.14 seconds versus
0.11 seconds for the previous version. Actual performance depends on
CPU, dictionary, dice distribution, and system load.

## Notes on compatibility

This is a replacement for the original `boggle.cpp` rather than a minimal
line-by-line edit. It preserves the original purpose and input formats but
changes the default output to per-length top-N rankings, with complete CSV
frequency output available using `--csv FILE`, and adds explicit options. Running with a fixed seed intentionally
produces **different random boards** from the original `rand()`/Boost streams.
This changes individual Monte Carlo estimates, but not the intended sampling
law. Monte Carlo estimates are not an exact enumeration of all boards.
