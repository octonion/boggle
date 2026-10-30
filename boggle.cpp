// Monte Carlo word-occurrence frequencies for standard Boggle dice.
// C++17; no external libraries required.
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <map>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr uint32_t absent = std::numeric_limits<uint32_t>::max();
constexpr uint32_t no_word = absent;

uint64_t parse_u64(const std::string& s, const char* option) {
    if (s.empty() || s[0] == '-') throw std::runtime_error(std::string("Invalid ") + option + ": " + s);
    std::size_t pos = 0;
    uint64_t result = 0;
    try { result = std::stoull(s, &pos); }
    catch (const std::exception&) { throw std::runtime_error(std::string("Invalid ") + option + ": " + s); }
    if (pos != s.size()) throw std::runtime_error(std::string("Invalid ") + option + ": " + s);
    return result;
}

struct Options {
    std::string cubes, dictionary;
    uint64_t simulations = 100000;
    uint64_t seed = 1;
    unsigned threads = 1;
    uint64_t top = 10;
    uint64_t min_length = 3;
    std::string csv;
};

void usage(std::ostream& out, const char* program) {
    out << "Usage: " << program
        << " CUBES.cub WORDS.txt [--sims N] [--threads N] [--seed N] [--top N] [--min-length N] [--csv FILE]\n"
        << "  --sims      Number of randomly shaken boards (default: 100000)\n"
        << "  --threads   Independent workers (default: 1)\n"
        << "  --seed      Reproducible random seed (default: 1)\n"
        << "  --top       Show N most frequent found words of each length (default: 10)\n"
        << "  --min-length  Minimum written word length, Qu counts as 2 (default: 3)\n"
        << "  --csv       Save all found words and their occurrence counts as word,frequency CSV\n"
        << "Supports 4x4 (16 dice) and 5x5 (25 dice) boards.\n"
        << "Each word is counted once per board; rankings use spelling length (Qu = 2 letters).\n";
}

Options arguments(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        usage(std::cout, argv[0]);
        std::exit(0);
    }
    if (argc < 3) {
        usage(std::cerr, argv[0]);
        throw std::runtime_error("Missing cube file or dictionary");
    }
    Options opt;
    opt.cubes = argv[1];
    opt.dictionary = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string key = argv[i];
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + key);
        std::string value = argv[++i];
        if (key == "--csv") {
            if (value.empty() || value == "-")
                throw std::runtime_error("--csv requires a file path, not standard output");
            opt.csv = value;
            continue;
        }
        if (key != "--sims" && key != "--seed" && key != "--threads" && key != "--top" && key != "--min-length")
            throw std::runtime_error("Unknown option: " + key);
        uint64_t n = parse_u64(value, key.c_str());
        if (key == "--sims") opt.simulations = n;
        else if (key == "--seed") opt.seed = n;
        else if (key == "--threads") {
            if (n == 0 || n > 1024) throw std::runtime_error("--threads must be between 1 and 1024");
            opt.threads = static_cast<unsigned>(n);
        } else if (key == "--top") {
            if (n == 0) throw std::runtime_error("--top must be positive");
            opt.top = n;
        } else {
            if (n == 0) throw std::runtime_error("--min-length must be positive");
            opt.min_length = n;
        }
    }
    if (!opt.csv.empty()) {
        std::error_code ec;
        if (std::filesystem::equivalent(opt.csv, opt.cubes, ec) ||
            std::filesystem::equivalent(opt.csv, opt.dictionary, ec))
            throw std::runtime_error("CSV output must not overwrite the dice or dictionary file");
    }
    return opt;
}

struct Dice {
    unsigned side = 0;
    std::vector<std::array<uint8_t, 6>> faces;
    std::vector<uint32_t> neighbors;
};

Dice load_dice(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) throw std::runtime_error("Cannot open cube file: " + filename);
    Dice dice;
    std::string cube;
    while (in >> cube) {
        if (cube.size() != 6) throw std::runtime_error("Each cube must have exactly six faces: " + cube);
        std::array<uint8_t, 6> faces{};
        for (unsigned i = 0; i < 6; ++i) {
            unsigned char ch = static_cast<unsigned char>(cube[i]);
            if (ch < 'a' || ch > 'z') throw std::runtime_error("Cube faces must be lowercase a-z: " + cube);
            faces[i] = static_cast<uint8_t>(ch - 'a');
        }
        dice.faces.push_back(faces);
    }
    if (dice.faces.size() == 16) dice.side = 4;
    else if (dice.faces.size() == 25) dice.side = 5;
    else throw std::runtime_error("Expected exactly 16 or 25 dice, got " + std::to_string(dice.faces.size()));
    unsigned n = dice.side;
    dice.neighbors.resize(n * n);
    for (unsigned r = 0; r < n; ++r) for (unsigned c = 0; c < n; ++c) {
        uint32_t mask = 0;
        for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
            int rr = static_cast<int>(r) + dr, cc = static_cast<int>(c) + dc;
            if ((dr || dc) && rr >= 0 && cc >= 0 && rr < static_cast<int>(n) && cc < static_cast<int>(n))
                mask |= uint32_t{1} << (rr * static_cast<int>(n) + cc);
        }
        dice.neighbors[r * n + c] = mask;
    }
    return dice;
}

// On a Boggle cube, q denotes Qu.  Reject dictionary entries with bare q.
// Words need at least three physical cubes, matching the original simulator.
std::string tile_spelling(const std::string& word) {
    std::string tiles;
    for (std::size_t i = 0; i < word.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(word[i]);
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch - 'A' + 'a');
        if (ch < 'a' || ch > 'z') return "";
        if (ch == 'q') {
            if (i + 1 >= word.size()) return "";
            unsigned char next = static_cast<unsigned char>(word[i + 1]);
            if (next != 'u' && next != 'U') return "";
            ++i;
        }
        tiles.push_back(static_cast<char>(ch));
    }
    return tiles.size() >= 3 ? tiles : "";
}

struct Trie {
    // BFS puts each node's children in one contiguous, alphabetically ordered
    // range. A 26-bit letter mask and its first index replace all explicit edges.
    struct Node { uint32_t first_child, mask, word; };
    struct BuildNode {
        std::array<uint32_t, 26> child;
        uint32_t word = no_word;
        BuildNode() { child.fill(absent); }
    };
    std::vector<Node> nodes;
    std::array<uint32_t, 26> root{};
    std::array<uint32_t, 26 * 26> pairs{};
    std::vector<std::string> words;
    // Alphabetical word index -> BFS terminal index. Only aggregation uses this
    // mapping; the search accesses the nearby counters for short words directly.
    std::vector<uint32_t> counter_ids;

    explicit Trie(const std::string& filename, uint64_t min_length) {
        std::ifstream in(filename);
        if (!in) throw std::runtime_error("Cannot open dictionary: " + filename);
        std::vector<std::pair<std::string, std::string>> entries;
        std::string word;
        while (in >> word) {
            std::string tiles = tile_spelling(word);
            if (tiles.empty() || word.size() < min_length) continue;
            std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            entries.emplace_back(word, std::move(tiles));
        }
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end(),
                      [](const auto& a, const auto& b) { return a.first == b.first; }), entries.end());
        if (entries.size() >= no_word) throw std::runtime_error("Too many dictionary entries");
        std::vector<BuildNode> build(1); // root
        for (const auto& entry : entries) {
            uint32_t v = 0;
            for (char ch : entry.second) {
                unsigned c = static_cast<unsigned>(ch - 'a');
                uint32_t child = build[v].child[c];
                if (child == absent) {
                    if (build.size() >= (uint32_t{1} << 27)) throw std::runtime_error("Trie too large");
                    child = static_cast<uint32_t>(build.size());
                    build.emplace_back();
                    build[v].child[c] = child; // after reallocation
                }
                v = child;
            }
            build[v].word = static_cast<uint32_t>(words.size());
            words.push_back(entry.first);
        }
        // Appending each parent's children together in alphabetical order gives
        // both good prefix locality and the contiguous-child invariant below.
        std::vector<uint32_t> order;
        order.reserve(build.size());
        order.push_back(0);
        for (size_t i = 0; i < order.size(); ++i) {
            const auto &child = build[order[i]].child;
            for (unsigned c = 0; c < 26; ++c)
                if (child[c] != absent) order.push_back(child[c]);
        }
        std::vector<uint32_t> renum(build.size());
        for (size_t i = 0; i < order.size(); ++i)
            renum[order[i]] = static_cast<uint32_t>(i);
        nodes.reserve(build.size());
        counter_ids.resize(words.size());
        uint32_t next_counter = 0;
        uint32_t next_child = 1;
        for (uint32_t v : order) {
            const BuildNode& b = build[v];
            // Give terminals BFS counter IDs while keeping words alphabetical
            // for the existing reports. Workers need no extra lookup on a hit.
            uint32_t word_id = no_word;
            if (b.word != no_word) {
                word_id = next_counter++;
                counter_ids[b.word] = word_id;
            }
            uint32_t first = next_child, mask = 0;
            for (unsigned c = 0; c < 26; ++c) {
                if (b.child[c] != absent) {
                    if (renum[b.child[c]] != next_child)
                        throw std::runtime_error("Noncontiguous BFS children");
                    ++next_child;
                    mask |= uint32_t{1} << c;
                }
            }
            nodes.push_back({first, mask, word_id});
        }
        for (int c = 0; c < 26; ++c)
            root[c] = build[0].child[c] == absent ? absent : renum[build[0].child[c]];
        // Dense lookup for the first two tiles avoids high-degree root scans.
        // No terminal is skipped: tile_spelling requires at least three dice.
        for (unsigned a = 0; a < 26; ++a)
            for (unsigned b = 0; b < 26; ++b)
                pairs[26 * a + b] = root[a] == absent ? absent : child(root[a], b);
    }

    // The number of smaller child letters is the offset in the BFS range.
    uint32_t child(uint32_t node_id, uint8_t letter) const {
        const Node& n = nodes[node_id];
        const uint32_t bit = uint32_t{1} << letter;
        return (n.mask & bit) ? n.first_child + __builtin_popcount(n.mask & (bit - 1)) : absent;
    }
};

// SplitMix64 supplies an independent, deterministic seed per board index.
uint64_t splitmix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint32_t next() {
        uint64_t old = state;
        state = old * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        uint32_t x = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (x >> rot) | (x << ((0u - rot) & 31u));
    }
    uint32_t uniform(uint32_t bound) {
        uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            uint32_t x = next();
            if (x >= threshold) return x % bound;
        }
    }
};

// All mutable data belongs to exactly one worker, including word-hit markers.
struct alignas(64) Worker {
    std::vector<uint64_t> counts;
    std::vector<uint32_t> seen;
    uint32_t epoch = 0;
    uint32_t neighbor_letters[25]{};

    // Inline 32-bit SWAR population count avoids a library call in generic
    // builds. GCC recognizes this idiom and uses POPCNT when the target enables
    // it (for example -march=znver3); no CPU-specific instruction is required.
    static unsigned count_bits(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        x = (x + (x >> 4)) & 0x0f0f0f0fu;
        return (x * 0x01010101u) >> 24;
    }

    // Specializing the fixed board geometry turns mask construction into direct
    // ORs, without a per-board loop over set bits of each adjacency mask.
    template<unsigned Side, unsigned Square>
    static uint32_t adjacent_letters(const uint32_t* bits) {
        constexpr unsigned r = Square / Side, c = Square % Side;
        uint32_t mask = 0;
        if constexpr (r > 0) {
            if constexpr (c > 0) mask |= bits[Square - Side - 1];
            mask |= bits[Square - Side];
            if constexpr (c + 1 < Side) mask |= bits[Square - Side + 1];
        }
        if constexpr (c > 0) mask |= bits[Square - 1];
        if constexpr (c + 1 < Side) mask |= bits[Square + 1];
        if constexpr (r + 1 < Side) {
            if constexpr (c > 0) mask |= bits[Square + Side - 1];
            mask |= bits[Square + Side];
            if constexpr (c + 1 < Side) mask |= bits[Square + Side + 1];
        }
        return mask;
    }

    template<unsigned Side, std::size_t... Squares>
    void prepare_neighbors(const uint32_t* bits, std::index_sequence<Squares...>) {
        ((neighbor_letters[Squares] = adjacent_letters<Side, Squares>(bits)), ...);
    }

    explicit Worker(std::size_t n) : counts(n, 0), seen(n, 0) {}

    void visit(const Trie& trie, const Dice& dice, const uint32_t* letter_positions,
               unsigned square, uint32_t node, uint32_t used) {
        uint32_t word = trie.nodes[node].word;
        if (word != no_word && seen[word] != epoch) {
            seen[word] = epoch;
            ++counts[word];
        }
        uint32_t possible = dice.neighbors[square] & ~used;
        const Trie::Node& n = trie.nodes[node];
        // This board-level filter includes already used adjacent squares.
        // It is conservative: matching below applies the exact unused mask.
        uint32_t children = n.mask & neighbor_letters[square];
        while (children) {
            unsigned letter = static_cast<unsigned>(__builtin_ctz(children));
            children &= children - 1;
            uint32_t matching = possible & letter_positions[letter];
            if (!matching) continue;
            // Rank only after finding an unused match, avoiding wasted work
            // when this letter occurs solely on already used adjacent squares.
            uint32_t child = n.first_child + count_bits(n.mask & ((uint32_t{1} << letter) - 1));
            while (matching) {
                unsigned next_square = static_cast<unsigned>(__builtin_ctz(matching));
                matching &= matching - 1;
                visit(trie, dice, letter_positions, next_square, child,
                      used | (uint32_t{1} << next_square));
            }
        }
    }

    void run(const Trie& trie, const Dice& dice, uint64_t seed, uint64_t first, uint64_t end) {
        std::array<uint8_t, 25> board{};
        std::array<uint8_t, 25> permutation{};
        unsigned n = static_cast<unsigned>(dice.faces.size());
        for (uint64_t iteration = first; iteration < end; ++iteration) {
            if (++epoch == 0) {
                std::fill(seen.begin(), seen.end(), 0);
                epoch = 1;
            }
            Rng rng(splitmix64(seed ^ splitmix64(iteration)));
            for (unsigned j = 0; j < n; ++j) permutation[j] = static_cast<uint8_t>(j);
            for (unsigned j = n - 1; j > 0; --j)
                std::swap(permutation[j], permutation[rng.uniform(j + 1)]);
            uint32_t letter_positions[26]{};
            uint32_t letter_bits[25];
            for (unsigned j = 0; j < n; ++j) {
                board[j] = dice.faces[permutation[j]][rng.uniform(6)];
                letter_positions[board[j]] |= uint32_t{1} << j;
                letter_bits[j] = uint32_t{1} << board[j];
            }
            if (dice.side == 4)
                prepare_neighbors<4>(letter_bits, std::make_index_sequence<16>{});
            else
                prepare_neighbors<5>(letter_bits, std::make_index_sequence<25>{});
            // Begin at depth two through the dense prefix table. All terminal
            // words use at least three physical dice, including words with Qu.
            for (unsigned j = 0; j < n; ++j) {
                uint32_t adjacent = dice.neighbors[j];
                const uint32_t* pairs = trie.pairs.data() + 26 * board[j];
                while (adjacent) {
                    unsigned k = static_cast<unsigned>(__builtin_ctz(adjacent));
                    adjacent &= adjacent - 1;
                    uint32_t child = pairs[board[k]];
                    if (child != absent)
                        visit(trie, dice, letter_positions, k, child,
                              (uint32_t{1} << j) | (uint32_t{1} << k));
                }
            }
        }
    }
};

// Occurrence counts are exact integers; only found words are reported.
// Each spelling-length group is ordered by decreasing frequency with
// alphabetic ties, so results are independent of thread count or platform.
void report_top(std::ostream& out, const Trie& trie, const std::vector<uint64_t>& totals,
                uint64_t simulations, uint64_t top) {
    std::map<std::size_t, std::vector<std::size_t>> by_length;
    for (std::size_t i = 0; i < totals.size(); ++i)
        if (totals[i]) by_length[trie.words[i].size()].push_back(i);
    if (by_length.empty()) {
        out << "No words found.\n";
        return;
    }
    for (auto& group : by_length) {
        auto& ids = group.second;
        auto better = [&](std::size_t a, std::size_t b) {
            if (totals[a] != totals[b]) return totals[a] > totals[b];
            return trie.words[a] < trie.words[b];
        };
        const auto limit = static_cast<std::size_t>(std::min<uint64_t>(top, ids.size()));
        std::partial_sort(ids.begin(), ids.begin() + limit, ids.end(), better);
        out << "Length " << group.first << " (" << ids.size()
            << (ids.size() == 1 ? " word found):\n" : " words found):\n");
        for (std::size_t j = 0; j < limit; ++j) {
            auto i = ids[j];
            out << "  " << trie.words[i] << "\t" << totals[i] << "\t(";
            out << std::fixed << std::setprecision(4)
                << (100.0 * static_cast<double>(totals[i]) / static_cast<double>(simulations))
                << "%)\n";
        }
        out << '\n';
    }
}

void save_csv(const std::string& filename, const Trie& trie,
              const std::vector<uint64_t>& totals) {
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot open CSV output: " + filename);
    file << "word,frequency\n";
    for (std::size_t i = 0; i < totals.size(); ++i)
        if (totals[i]) file << trie.words[i] << ',' << totals[i] << '\n';
    file.close();
    if (!file) throw std::runtime_error("Failed to write CSV output: " + filename);
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options opt = arguments(argc, argv);
        auto load_start = std::chrono::steady_clock::now();
        Dice dice = load_dice(opt.cubes);
        Trie trie(opt.dictionary, opt.min_length);
        auto load_end = std::chrono::steady_clock::now();
        unsigned workers_count = static_cast<unsigned>(
            std::min<uint64_t>(opt.threads, std::max<uint64_t>(opt.simulations, 1)));
        std::vector<Worker> workers;
        workers.reserve(workers_count);
        for (unsigned i = 0; i < workers_count; ++i) workers.emplace_back(trie.words.size());
        auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> threads;
        threads.reserve(workers_count > 0 ? workers_count - 1 : 0);
        for (unsigned id = 1; id < workers_count; ++id) {
            uint64_t first = opt.simulations / workers_count * id
                           + std::min<uint64_t>(id, opt.simulations % workers_count);
            uint64_t count = opt.simulations / workers_count + (id < opt.simulations % workers_count);
            threads.emplace_back(&Worker::run, &workers[id], std::cref(trie), std::cref(dice),
                                 opt.seed, first, first + count);
        }
        uint64_t count0 = opt.simulations / workers_count + (0 < opt.simulations % workers_count);
        workers[0].run(trie, dice, opt.seed, 0, count0);
        for (auto& t : threads) t.join();
        auto end = std::chrono::steady_clock::now();
        std::vector<uint64_t> totals(trie.words.size(), 0);
        // Restore alphabetical dictionary indices once, outside the search.
        for (const auto& worker : workers)
            for (std::size_t j = 0; j < totals.size(); ++j)
                totals[j] += worker.counts[trie.counter_ids[j]];
        report_top(std::cout, trie, totals, opt.simulations, opt.top);
        if (!opt.csv.empty()) save_csv(opt.csv, trie, totals);
        double loading = std::chrono::duration<double>(load_end - load_start).count();
        double simulation = std::chrono::duration<double>(end - start).count();
        std::cerr << "Dictionary: " << trie.words.size() << " words, " << trie.nodes.size()
                  << " trie nodes, " << (trie.nodes.size() - 1) << " edges; load " << loading << " s\n"
                  << "Simulated " << opt.simulations << " boards on " << workers_count << " worker(s)"
                  << " in " << simulation << " s";
        if (simulation > 0) std::cerr << " (" << static_cast<double>(opt.simulations) / simulation << " boards/s)";
        std::cerr << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
