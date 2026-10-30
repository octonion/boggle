# Boggle word-frequency simulator

A corrected, parallel C++17 implementation of `octonion/boggle`'s Monte Carlo
simulator. Given a physical Boggle dice set and a word list, it estimates how
often each dictionary word appears on a randomly shaken board. Each word counts
**once per board**, even if it can be spelled by several paths.

## Build and run

Requires a C++17 compiler and standard POSIX thread support; no Boost dependency.

```sh
make
./boggle Boggle83.cub words.txt --sims 100000 --threads 4 --seed 1 > frequencies.csv
```

The original two-argument invocation still works, with defaults of 100,000
simulations, one worker, and seed 1:

```sh
./boggle Boggle83.cub words.txt > frequencies.csv
```

Output is `word,count`, sorted alphabetically; only words with nonzero counts
are printed. Build and simulation timings are written to stderr. Probability
estimates are `count / simulations`.

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
* Each worker maintains private visited-word stamps and private counts; no
  dictionary mutations or shared hot counters occur during simulation.
* The board visit set is a 32-bit local mask; recursive searches neither
  reuse a cell nor modify shared search state.
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
prefix/longer words, duplicate paths, malformed dice, and empty dictionaries.

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

The benchmark checks that all thread counts produce identical output. For
speedups, use enough boards that initialization and thread-startup overhead
are negligible. A 16-core machine is best characterized by 1, 2, 4, 8 and
16 workers. SMT scaling past 16 workers is an additional experiment.

## Notes on compatibility

This is a replacement for the original `boggle.cpp` rather than a minimal
line-by-line edit. It preserves the original purpose and input formats but
changes the output to a clean CSV stream, removes thread summary lines from
stdout, and adds explicit options. Running with a fixed seed intentionally
produces **different random boards** from the original `rand()`/Boost streams.
This changes individual Monte Carlo estimates, but not the intended sampling
law. Monte Carlo estimates are not an exact enumeration of all boards.
