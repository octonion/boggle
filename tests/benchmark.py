#!/usr/bin/env python3
"""Benchmark thread scaling; verify identical outputs across worker counts."""
import hashlib
import re
import subprocess
import sys

if len(sys.argv) < 4:
    print('Usage: benchmark.py BINARY DICE DICTIONARY [SIMS=50000] [SEED=42]', file=sys.stderr)
    sys.exit(2)
exe, dice, dictionary = sys.argv[1:4]
sims = int(sys.argv[4]) if len(sys.argv) > 4 else 50000
seed = int(sys.argv[5]) if len(sys.argv) > 5 else 42
baseline = None
baseline_seconds = None
print('workers,seconds,boards_per_second,speedup,sha256_match')
for threads in (1, 2, 4, 8, 16):
    cmd = [exe, dice, dictionary, '--sims', str(sims), '--threads', str(threads), '--seed', str(seed)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    m = re.search(rb'Simulated \d+ boards on \d+ worker\(s\) in ([0-9.e+-]+) s', proc.stderr)
    if not m:
        raise ValueError('Could not parse timing:\n' + proc.stderr.decode())
    seconds = float(m.group(1))
    digest = hashlib.sha256(proc.stdout).hexdigest()
    if baseline is None:
        baseline = digest
        baseline_seconds = seconds
    match = digest == baseline
    print(f'{threads},{seconds:.6f},{sims/seconds:.1f},{baseline_seconds/seconds:.2f},{match}')
    if not match:
        sys.exit('FAIL: changing thread count changed results')
