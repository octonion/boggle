# Boggle word-frequency simulator

A high-performance, parallel C++17 Monte Carlo simulator for the frequencies
of words on randomly shaken Boggle boards. It builds on the original C++
program with faster trie traversal, a compact dictionary representation,
reproducible sampling, and efficient multicore execution.

Each simulated board is created by uniformly shuffling the physical dice and
independently selecting one of the six faces of each die. A word counts **once
per board**, even if multiple paths spell it.

## Build and run

Requires a C++17 compiler and POSIX threads. No external libraries are needed.

```sh
make
./boggle Boggle83.cub words.txt --sims 1000000 --threads 16 --seed 42
```

The two positional arguments are the **dice file** and **dictionary**.
The default run uses 100,000 boards, one worker, seed 1, and the top 10
found words of each spelling length.

| Option | Meaning | Default |
| --- | --- | --- |
| `--sims N` | Number of independently generated boards | `100000` |
| `--threads N` | Number of workers | `1` |
| `--seed N` | Reproducible 64-bit random seed | `1` |
| `--top N` | Maximum displayed words per spelling length | `10` |
| `--csv FILE` | Additionally save complete `word,frequency` data | None |

For example, to display up to 20 words of each length and retain all
nonzero frequencies:

```sh
./boggle Boggle83.cub words.txt --sims 1000000 --threads 32 \
    --seed 42 --top 20 --csv frequencies.csv
```

Standard output lists lengths from shortest to longest, and within each
length displays words in descending order of the number of boards on which
they appear. Equal counts are ordered alphabetically. Groups with fewer
than `N` found words display them all; lengths with no discoveries are
omitted. A row shows the word, its integer count, and its estimated
probability (as a percentage of all simulated boards).

`--csv FILE` writes an alphabetically sorted CSV file with the header
`word,frequency`. Here **frequency means the integer count of boards
containing the word**, not its percentage; only positive counts are
included. The top-N summary still goes to stdout. Dictionary-loading
and simulation timings go to stderr. Use `./boggle --help` for help.

## Dice sets

Each `.cub` file contains one lowercase six-character string per die.
Each character denotes one equally likely face; `q` represents the
two-letter tile **Qu**. The dice are placed in uniformly random positions
on the board. The supplied files are:

| File | Edition represented | Grid | Dice | Notes |
| --- | --- | --- | ---: | --- |
| [`Boggle83.cub`](Boggle83.cub) | Boggle 1983 (classic distribution) | 4×4 | 16 | The original/classic letter assortment, used before the late-1980s redesign. |
| [`Boggle92.cub`](Boggle92.cub) | Boggle 1992 (revised distribution) | 4×4 | 16 | The redesigned assortment, introduced around 1987 and also used in 1992 editions. |
| [`Big.cub`](Big.cub) | Big Boggle | 5×5 | 25 | Earlier 25-die distribution; includes a repeated `dhlnor` die. |
| [`Deluxe.cub`](Deluxe.cub) | Boggle Deluxe | 5×5 | 25 | Later 25-die distribution, differing from `Big.cub` in four dice. |
| [`Master.cub`](Master.cub) | Boggle Master | 5×5 | 25 | **Identical contents to `Deluxe.cub`**; these produce the same board distribution. |

The two 4×4 sets have different letter frequencies and therefore different
word-occurrence probabilities. The 5×5 Big and Deluxe/Master sets also
differ: the four Big Boggle dice `ccenst`, `ddhnot`, `dhlnor`
(one additional copy), and `iprrry` are replaced in Deluxe/Master by
`ccnstw`, `ddlnor`, `dhhnot`, and `hiprry`, respectively.

**Word-length convention:** The simulator requires a word to use at least
**three physical dice**, for either board size. Standard 5×5 Big Boggle
rules normally require at least **four written letters**; the program
does not impose that extra edition-specific restriction. When interpreting
5×5 output as playable Big Boggle words, omit three-letter entries.
For example, `qua` uses just two dice (`Qu` and `a`) and is excluded
by the simulator. Word-length rankings count the written `Qu` as two
letters. Special-edition challenge/bonus scoring is not simulated.

For descriptions and historical comparisons of the letter distributions,
see [Bananagrammer's classic-versus-revised dice comparison](https://www.bananagrammer.com/2013/10/the-boggle-cube-redesign-and-its-effect.html)
and [BoardGameGeek's Big Boggle entry](https://boardgamegeek.com/boardgame/10550/big-boggle).

## Dictionary

The bundled [`words.txt`](words.txt) is based on **TWL06**, the
**2006 North American Scrabble Tournament Word List** (178,691 words).
A direct, case-insensitive comparison with an independently published
TWL06 file confirms that this repository's file contains **178,652
distinct words**: it is the full TWL06 list except for 39 entries whose
`q` is not immediately followed by `u`. Three more such entries remain
in the file but are ignored by the simulator.

After excluding those three entries and 101 entries requiring fewer than
three Boggle dice, the program loads **178,548 words**, as shown in its
startup diagnostics. Thus `words.txt` is a *Boggle-adapted TWL06 list*,
rather than the current Scrabble dictionary or an arbitrary English word
list. The original source of this repository's copy is not recorded.

The loader also accepts other whitespace-separated word lists:
case is ignored, repeated words are deduplicated, and entries containing
nonletters are discarded. Each occurrence probability depends on the chosen
dictionary as well as the dice set.

For information about TWL06 and other Boggle/Scrabble word lists, see
[Vanderkam's word-list notes](https://github.com/danvk/hybrid-boggle/blob/main/wordlists/README.md).

## Implementation and performance optimizations

The program retains trie-based depth-first search but reorganizes the
data and computation for fast single-core and multicore execution:

- **Compact shared trie:** The immutable trie is stored in contiguous
  breadth-first order, so frequently visited short prefixes have better
  locality. Nodes hold offsets into an edge array, rather than 26 child
  pointers each. Each edge packs a 5-bit letter and a child-node index
  into **32 bits**, reducing memory traffic.
- **Bitmask search:** A 32-bit mask tracks used board positions (up to 25
  tiles). Each board has 26 precomputed masks recording the positions of
  each letter. DFS walks actual outgoing trie edges and intersects their
  letter masks with adjacent unused positions, instead of probing every
  neighboring square for a child that usually does not exist.
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

## Ryzen 9 5950X benchmarks

Measured on an AMD Ryzen 9 5950X (16 physical cores, 32 logical threads)
using GCC `-O3`, `Boggle83.cub`, **178,548 loaded words**,
**1,000,000 boards**, and `--seed 42`.

| Workers | Simulation time | Boards/second | Speedup vs. 1 worker |
| ---: | ---: | ---: | ---: |
| 1 | 8.924 s | 112,054 | 1.00× |
| 2 | 4.452 s | 224,627 | 2.00× |
| 4 | 2.346 s | 426,288 | 3.80× |
| 8 | 1.220 s | 819,997 | 7.32× |
| 16 | 0.635 s | 1,575,450 | 14.06× |
| 32 | **0.435 s** | **2,300,200** | **20.53×** |

The times measure **simulation only**; they exclude loading the
dictionary (about 0.14 s with this layout) and printing results.
These are single benchmark runs rather than statistical summaries, so
small timing differences can vary with system load. At 16 workers,
parallel efficiency relative to one worker was about **88%**. Using
both hardware threads per core improved throughput by about **46%**
over 16 workers. The breadth-first layout and packed edges gave about
**17–21% greater throughput** than the preceding implementation across
the tested worker counts.

On the same hardware, 32 workers are therefore worthwhile for long
simulations, although the best count depends on CPU, memory hierarchy,
other workloads, and dictionary size. For short runs, dictionary loading
and thread-startup time can dominate.

## Tests and benchmarking

```sh
make test
python3 tests/benchmark.py ./boggle Boggle83.cub words.txt 50000 42
```

The test suite includes an independent Python reference implementation,
4×4 and 5×5 boards, Qu handling, duplicate word paths, different thread
counts, input validation, top-N sorting and CSV output. The benchmark
script checks that both the ranking and full CSV frequencies are identical
across the tested worker counts.

For additional memory-safety testing (GCC or Clang):

```sh
g++ -std=c++17 -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -pthread -o boggle_san boggle.cpp
python3 tests/test_boggle.py ./boggle_san
```

## Compatibility with the original simulator

The original two-positional-argument invocation still works. This version
adds explicit simulation, thread, seed, and reporting options, and makes
the concise per-length ranking the default output. Full counts can be
saved with `--csv FILE`. Different pseudorandom generators mean that a
given seed need not reproduce the original program's exact random boards;
the intended uniform dice-shuffling and face-selection distributions are
the same. Monte Carlo frequency estimates are not an exact enumeration
of all boards.
