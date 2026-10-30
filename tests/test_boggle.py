#!/usr/bin/env python3
"""Deterministic regression and independent Boggle-search oracle tests."""
import collections
import pathlib
import subprocess
import sys
import tempfile
import unittest

EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else pathlib.Path('./boggle').resolve()
sys.argv = [sys.argv[0]]
MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1


def mix(x):
    x = (x + 0x9e3779b97f4a7c15) & MASK64
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & MASK64
    return x ^ (x >> 31)


class RNG:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        old = self.state
        self.state = (old * 6364136223846793005 + 1442695040888963407) & MASK64
        x = (((old >> 18) ^ old) >> 27) & MASK32
        rot = old >> 59
        return ((x >> rot) | (x << ((-rot) & 31))) & MASK32

    def uniform(self, bound):
        threshold = (1 << 32) % bound
        while True:
            x = self.next()
            if x >= threshold:
                return x % bound


def board_from_seed(dice, seed, iteration):
    rng = RNG(mix(seed ^ mix(iteration)))
    ids = list(range(len(dice)))
    for j in range(len(dice) - 1, 0, -1):
        k = rng.uniform(j + 1)
        ids[j], ids[k] = ids[k], ids[j]
    return ''.join(dice[i][rng.uniform(6)] for i in ids)


def tiles(word):
    word = word.lower()
    result = []
    i = 0
    while i < len(word):
        ch = word[i]
        if not ('a' <= ch <= 'z'):
            return ''
        if ch == 'q':
            if i + 1 == len(word) or word[i + 1] != 'u':
                return ''
            i += 1
        result.append(ch)
        i += 1
    return ''.join(result) if len(result) >= 3 else ''


def exists_on_board(board, word):
    side = 4 if len(board) == 16 else 5
    seq = tiles(word)
    if not seq:
        return False

    def search(k, pos, seen):
        if k == len(seq):
            return True
        r, c = divmod(pos, side)
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                rr, cc = r + dr, c + dc
                if 0 <= rr < side and 0 <= cc < side:
                    j = rr * side + cc
                    if not ((seen >> j) & 1) and board[j] == seq[k]:
                        if search(k + 1, j, seen | (1 << j)):
                            return True
        return False

    return any(ch == seq[0] and search(1, i, 1 << i) for i, ch in enumerate(board))


class BoggleTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = pathlib.Path(self.tempdir.name)

    def make(self, dice, words):
        cub = self.root / 'dice.cub'
        dic = self.root / 'dictionary.txt'
        cub.write_text('\n'.join(dice) + '\n')
        dic.write_text('\n'.join(words) + '\n')
        return cub, dic

    def invoke(self, dice, words, sims, threads, seed=1, success=True):
        cube, dictionary = self.make(dice, words)
        csv_path = self.root / 'all_frequencies.csv'
        result = subprocess.run([str(EXE), str(cube), str(dictionary),
                                 '--sims', str(sims), '--threads', str(threads), '--seed', str(seed),
                                 '--csv', str(csv_path)],
                                capture_output=True, text=True)
        if not success:
            self.assertNotEqual(result.returncode, 0, result.stderr)
            return result
        self.assertEqual(result.returncode, 0, result.stderr)
        counts = {}
        self.assertTrue(csv_path.exists())
        rows = csv_path.read_text().splitlines()
        self.assertEqual(rows[0], 'word,frequency')
        for line in rows[1:]:
            key, amount = line.split(',')
            counts[key] = int(amount)
        self.assertEqual(list(counts), sorted(counts))
        self.assertIn('Simulated ', result.stderr)
        return counts

    def test_prefix_words_and_distinct_paths(self):
        dice = ['aaaaaa'] * 16
        words = ['aa', 'aaa', 'aaaa', 'aaaaa', 'a'*16, 'a'*17, 'aaa', 'a', 'qqq']
        for threads in (1, 2, 3, 4, 8):
            with self.subTest(threads=threads):
                self.assertEqual(self.invoke(dice, words, 17, threads),
                                 {'aaa': 17, 'aaaa': 17, 'aaaaa': 17, 'a'*16: 17})

    def test_qu_tiles(self):
        dice = ['qqqqqq'] * 15 + ['aaaaaa']
        words = ['qu', 'qua', 'ququ', 'quququ', 'quququa', 'QUQUQU', 'qaa', 'quaq', 'aaaa']
        counts = self.invoke(dice, words, 23, 4)
        self.assertEqual(counts['quququ'], 23)
        self.assertEqual(counts['quququa'], 23)
        self.assertNotIn('qua', counts)  # only two physical tiles
        self.assertNotIn('ququ', counts)
        self.assertNotIn('qaa', counts)  # bare q must be rejected
        self.assertNotIn('quaq', counts)
        self.assertNotIn('aaaa', counts)

    def test_independent_reference_oracle_4x4(self):
        dice = [x*6 for x in 'aabbccddeeffgghh']
        words = ['abc', 'abc', 'ace', 'bad', 'bed', 'cab', 'cafe', 'face',
                 'feed', 'fade', 'bead', 'aged', 'dead', 'egg', 'dad',
                 'def', 'abcdef', 'decaf', 'aabb', 'a', 'qq', 'QA', 'abc!']
        seed = 97831
        sims = 70
        reference = collections.Counter()
        for i in range(sims):
            board = board_from_seed(dice, seed, i)
            for word in set(words):
                if exists_on_board(board, word):
                    reference[word.lower()] += 1
        for threads in (1, 2, 3, 4, 8, 16):
            with self.subTest(threads=threads):
                self.assertEqual(self.invoke(dice, words, sims, threads, seed), dict(reference))

    def test_independent_reference_oracle_5x5(self):
        dice = [x*6 for x in 'aaaaabbbbbcccccdddddeeeee']
        words = ['aaa', 'bbb', 'ccc', 'ddd', 'eee', 'abc', 'edc',
                 'deed', 'abed', 'edged', 'abcde', 'abcdeabc', 'a'*25, 'a'*26]
        reference = collections.Counter()
        for i in range(31):
            board = board_from_seed(dice, 791, i)
            for word in set(words):
                if exists_on_board(board, word):
                    reference[word] += 1
        for threads in (1, 3, 5):
            self.assertEqual(self.invoke(dice, words, 31, threads, 791), dict(reference))

    def test_top_ranking_and_optional_csv(self):
        # Every board has 15 a's and 1 b. All four of these 3-letter
        # words are present with the same frequency, so ties are alphabetic.
        dice = ['aaaaaa'] * 15 + ['bbbbbb']
        words = ['baa', 'bbb', 'aaa', 'aba', 'aab', 'aaaa', 'aaaaa', 'abb']
        cube, dictionary = self.make(dice, words)
        summaries = []
        for threads in (1, 2, 4, 16):
            result = subprocess.run([str(EXE), str(cube), str(dictionary),
                                     '--sims', '19', '--threads', str(threads),
                                     '--seed', '42', '--top', '2'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Length 3 (4 words found):', result.stdout)
            self.assertIn('Length 4 (1 word found):', result.stdout)
            self.assertIn('Length 5 (1 word found):', result.stdout)
            self.assertNotIn('bbb', result.stdout)
            lines = result.stdout.splitlines()
            marker = lines.index('Length 3 (4 words found):')
            self.assertEqual([line.strip().split('\t')[0] for line in lines[marker+1:marker+3]],
                             ['aaa', 'aab'])
            self.assertIn('19\t(100.0000%)', lines[marker+1])
            self.assertNotIn('\n  aba\t', result.stdout)
            self.assertNotIn('\n  baa\t', result.stdout)
            summaries.append(result.stdout)
        self.assertTrue(all(x == summaries[0] for x in summaries))

        # Default top count is 10, and it does not truncate short groups.
        default = subprocess.run([str(EXE), str(cube), str(dictionary),
                                  '--sims', '19', '--threads', '1', '--seed', '42'],
                                 capture_output=True, text=True)
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertIn('\n  baa\t', default.stdout)
        self.assertIn('\n  aba\t', default.stdout)
        self.assertFalse((self.root / 'all_frequencies.csv').exists())

    def test_csv_and_validation(self):
        dice = ['aaaaaa'] * 16
        cube, dictionary = self.make(dice, ['aaa', 'aaaa', 'aab'])
        outfile = self.root / 'frequencies.csv'
        cmd = [str(EXE), str(cube), str(dictionary), '--sims', '23',
               '--threads', '3', '--seed', '42']
        result = subprocess.run(cmd + ['--csv', str(outfile)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(outfile.read_text(), 'word,frequency\naaa,23\naaaa,23\n')
        self.assertIn('Length 3', result.stdout)
        for extra in (['--top', '0'], ['--top', '-1'], ['--top', '1x'],
                      ['--csv', '-'], ['--csv', str(dictionary)], ['--csv', str(dictionary.parent / '.' / dictionary.name)],
                      ['--csv', str(self.root/'no_such_dir'/'data.csv')],
                      ['--unknown', '5']):
            bad = subprocess.run(cmd + extra, capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0, (extra, bad.stderr))

        no_words = subprocess.run([str(EXE), str(cube), str(dictionary),
                                   '--sims', '0', '--threads', '3'],
                                  capture_output=True, text=True)
        self.assertEqual(no_words.returncode, 0)
        self.assertEqual(no_words.stdout, 'No words found.\n')

    def test_minimum_written_word_length(self):
        # Qu counts as two written letters but occupies a single physical tile.
        dice = ['qqqqqq'] * 8 + ['aaaaaa'] * 8
        cube, dictionary = self.make(dice, ['aaa', 'aaaa', 'quaa', 'quaaa', 'quququ', 'quququa', 'qua'])
        snapshots = {}
        for min_length in (3, 4, 5, 6, 99):
            outputs = []
            for threads in (1, 4, 8):
                csv = self.root / 'min.csv'
                proc = subprocess.run([str(EXE), str(cube), str(dictionary),
                                       '--sims', '31', '--threads', str(threads), '--seed', '178',
                                       '--min-length', str(min_length), '--csv', str(csv)],
                                      capture_output=True, text=True)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                outputs.append((proc.stdout, csv.read_text()))
            self.assertTrue(all(x == outputs[0] for x in outputs))
            snapshots[min_length] = outputs[0][1]
        self.assertEqual(snapshots[3], 'word,frequency\naaa,31\naaaa,31\nquaa,31\nquaaa,31\nquququ,31\nquququa,31\n')
        self.assertEqual(snapshots[4], 'word,frequency\naaaa,31\nquaa,31\nquaaa,31\nquququ,31\nquququa,31\n')
        self.assertEqual(snapshots[5], 'word,frequency\nquaaa,31\nquququ,31\nquququa,31\n')
        self.assertEqual(snapshots[6], 'word,frequency\nquququ,31\nquququa,31\n')
        self.assertEqual(snapshots[99], 'word,frequency\n')
        # A minimum below 3 does not override the three-physical-dice rule.
        proc = subprocess.run([str(EXE), str(cube), str(dictionary),
                               '--sims', '31', '--min-length', '2'],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn('\n  qua\t', proc.stdout)
        for invalid in ('0', '-1', '3x'):
            bad = subprocess.run([str(EXE), str(cube), str(dictionary),
                                  '--min-length', invalid], capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)

    def test_validation_and_empty_dictionary(self):
        self.invoke(['aaaaaa'] * 15, ['aaa'], 1, 1, success=False)
        self.invoke(['aaaaaa'] * 15 + ['aaaaba!'], ['aaa'], 1, 1, success=False)
        self.assertEqual(self.invoke(['aaaaaa'] * 16, ['aa', 'a!', 'qxy'], 5, 2), {})
        self.assertEqual(self.invoke(['aaaaaa'] * 16, ['aaa'], 0, 4), {})


if __name__ == '__main__':
    unittest.main(verbosity=2)
