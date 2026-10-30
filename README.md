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

The Makefile automatically chooses a local, architecture-optimized build.
On a Xeon Phi (Knights Landing), it uses **`g++-14 -march=knl`**;
on the Ryzen 9 5950X and other processors it uses the system default
**`g++ -march=native`**, without pinning an older compiler. GCC 15 and later
removed support for `-march=knl`, so install `g++-14` on the Xeon Phi
(`sudo apt install g++-14`). A GCC 14 KNL deprecation warning is expected.

```sh
make show-config             # Show detected architecture, compiler and flags
make                         # Automatically chooses compiler and -march
make ARCH=znver3             # Explicit Ryzen 9 5950X / Zen 3 target
make ARCH=knl                # Explicit KNL target (automatically uses g++-14)
make ARCH=portable           # Compiler-default portable instruction set
make CXX=g++-16 ARCH=native  # Explicit compiler override
```

`ARCH=auto` is the default. Manual `CXX`, `CXXFLAGS` and `ARCH`
overrides take precedence; `ARCH=native` always targets the build machine.
The Makefile tracks the selected compiler version and flags and automatically
rebuilds `boggle` when these change, so `make clean` is not required when
switching targets. CPU-specific binaries are not necessarily portable to
other machines. The bitmap search benefits from hardware population counts
where the selected target supports them.

The two positional arguments are the **dice file** and **dictionary**.
The default run uses 100,000 boards, one worker thread, seed 1, a minimum of three
written letters (and three physical dice), and the top 10 found words of each
spelling length.

| Option | Meaning | Default |
| --- | --- | --- |
| `--sims N` | Number of independently generated boards | `100000` |
| `--threads N` | Number of worker threads | `1` |
| `--seed N` | Reproducible 64-bit random seed | `1` |
| `--top N` | Maximum displayed words per spelling length | `10` |
| `--min-length N` | Minimum written word length (Qu counts as two letters) | `3` |
| `--csv FILE` | Additionally save complete `word,frequency` data | None |

For example, to display up to 20 words of each length and retain all
nonzero frequencies:

```sh
./boggle Boggle83.cub words.txt --sims 1000000 --threads 32 \
    --seed 42 --top 20 --csv frequencies.csv
```

To use the traditional Big Boggle minimum of four written letters, run
`./boggle Big.cub words.txt --min-length 4`. The limit filters the dictionary
**before** trie construction, not during board search; thus it adds no work to
each simulated board and can slightly reduce search cost. Independently of
this option, a legal word must still occupy at least three physical dice.

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

## Dice sets and edition history

Each `.cub` file contains one lowercase six-character string per die;
each character denotes an equally likely face. `q` represents the
two-letter tile **Qu**. The original filenames are retained because the
same names were recorded in a [2004 Boggle-programming archive](https://www.gtoal.com/wordgames/boggle.html),
which explicitly credits **Chris Long** with supplying all five exact cube
lists. Thus `Boggle83.cub` and `Boggle92.cub` are *edition labels*, not
necessarily the introduction years of their letter distributions.

| File | Associated game / evidence for date | Grid | Dice |
| --- | --- | --- | ---: |
| [`Boggle83.cub`](Boggle83.cub) | **Classic Boggle**: used in the late 1970s and the documented **1983** edition | 4×4 | 16 |
| [`Boggle92.cub`](Boggle92.cub) | **Revised Boggle**: new distribution documented from **1987**, including **1992** editions | 4×4 | 16 |
| [`Big.cub`](Big.cub) | **Big Boggle**: product introduced **1979**, with this original-style 25-die list documented in the 2004 archive | 5×5 | 25 |
| [`Master.cub`](Master.cub) | **Boggle Master**: documented **1993** Parker Brothers edition; exact list in the 2004 archive | 5×5 | 25 |
| [`Deluxe.cub`](Deluxe.cub) | **Boggle Deluxe**: documented **1997** Parker Brothers edition; exact list in the 2004 archive | 5×5 | 25 |

The distinction between *product introduction*, *edition date*, and *dice
redesign* matters. The **1983** classic distribution predates that edition.
Parker Brothers' **1987** box advertised a revised letter assortment, so
the revised distribution was not first introduced in 1992.
**1979** is the verified Big Boggle product launch; the exact first issue
of every individual 25-die configuration is less firmly documented.
The **1993 Master** and **1997 Deluxe** dates identify verified editions,
not independently established first uses of their names or dice.

The files `Deluxe.cub` and `Master.cub` are **byte-for-byte identical**.
The 25-die `Big.cub` distribution differs in four dice: `ccenst`,
`ddhnot`, one additional `dhlnor`, and `iprrry` instead of
`ccnstw`, `ddlnor`, `dhhnot`, and `hiprry`, respectively.

**Rules note:** Standard 4×4 Boggle allows words of at least three
written letters; 5×5 Big Boggle normally requires at least **four**.
The simulator retains its original independent requirement of at least
three **physical dice** per word. The default `--min-length 3` preserves
its prior behavior for either board size; use `--min-length 4` for the
usual Big Boggle written-length cutoff. For example, `qua` uses only
two physical dice (`Qu` and `a`) and is excluded even at a lower
`--min-length`. Special-edition bonus scoring is not simulated.

Historical references:

- [Chris Long's five 2004 dice transcriptions, preserved by the Boggle-programming archive](https://www.gtoal.com/wordgames/boggle.html)
- [Bananagrammer: 1970s/1983 versus 1987 redesign, dice comparison](https://www.bananagrammer.com/2013/10/the-boggle-cube-redesign-and-its-effect.html)
- [BoardGameGeek: 1987 package advertised a revised letter assortment](https://boardgamegeek.com/thread/300565/review-from-a-boggle-veteran-and-beware-different)
- [Winning Moves: Big Boggle debuted in 1979](https://winning-moves.com/images/BigBoggle_Rules_2023.pdf)
- [BoardGameGeek: Boggle Master 1993 edition](https://boardgamegeek.com/boardgameversion/23978/parker-brothers-englishfrench-edition-1993)
- [BoardGameGeek: Boggle Deluxe 1997 edition](https://boardgamegeek.com/boardgameversion/26118/parker-brothers-english-edition-1997)

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

The loader applies the optional `--min-length N` filter to written
spelling length **before** building the trie, so higher cutoffs can
reduce memory use and search work. `Qu` contributes two written
letters but occupies one die; the minimum of three physical dice
remains in effect.

The loader also accepts other whitespace-separated word lists:
case is ignored, repeated words are deduplicated, and entries containing
nonletters are discarded. Each occurrence probability depends on the chosen
dictionary as well as the dice set.

For information about TWL06 and other Boggle/Scrabble word lists, see
[Vanderkam's word-list notes](https://github.com/danvk/hybrid-boggle/blob/main/wordlists/README.md).

## Implementation

The simulator shares an immutable, breadth-first trie among CPU **worker
threads**. Compact child-letter bitmaps, board-neighborhood masks, and a
two-letter lookup table reduce unnecessary search. Each thread maintains
private word counters and visit markers; the search loop requires no
inter-thread synchronization. Boards are generated deterministically from
their global simulation indices, so results do not depend on thread count.

For the trie layout, counter organization, memory accounting, and algorithmic
details, see [Implementation and performance](docs/performance.md).

## Performance

On a Ryzen 9 5950X, the current CPU solver achieved approximately **3.75
million boards/second with 32 threads** (median of three one-million-board
runs, Boggle83 dice, 178,548-word dictionary, GCC with
`-O3 -march=znver3`). Rates depend on the machine and workload.

See [performance measurements and methods](docs/performance.md#ryzen-9-5950x-benchmarks)
and the [experimental benchmark archive](benchmarks/2026-10-30/README.md).

## Tests

```sh
make test
python3 tests/benchmark.py ./boggle Boggle83.cub words.txt 50000 42
```

Tests cover 4×4 and 5×5 boards, an independent reference solver, Qu tiles,
duplicate paths, reproducibility across thread counts, and reporting.
For two-version comparisons, sanitizer commands, and benchmark methodology,
see [Performance reproduction and correctness](docs/performance.md#performance-reproduction-and-correctness).

## Compatibility with the original simulator

The original two-positional-argument invocation still works. This version
adds explicit simulation, thread, seed, and reporting options, and makes
the concise per-length ranking the default output. Full counts can be
saved with `--csv FILE`. Different pseudorandom generators mean that a
given seed need not reproduce the original program's exact random boards;
the intended uniform dice-shuffling and face-selection distributions are
the same. Monte Carlo frequency estimates are not an exact enumeration
of all boards.
