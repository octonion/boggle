#!/usr/bin/env python3
"""Interleave version benchmarks and verify complete frequency tables."""
import argparse
import hashlib
import json
import pathlib
import platform
import random
import re
import statistics
import subprocess
import tempfile


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=pathlib.Path)
    parser.add_argument('candidate', type=pathlib.Path)
    parser.add_argument('dice', type=pathlib.Path)
    parser.add_argument('dictionary', type=pathlib.Path)
    parser.add_argument('--sims', type=int, default=1000000)
    parser.add_argument('--threads', default='1,16,32', help='comma-separated worker counts')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--min-length', type=int, default=3)
    parser.add_argument('--json', type=pathlib.Path, help='also save raw timings and hashes')
    args = parser.parse_args()
    try:
        workers = [int(x) for x in args.threads.split(',')]
    except ValueError:
        parser.error('--threads must contain comma-separated integers')
    if args.sims <= 0 or args.repeat <= 0 or not workers or any(x < 1 or x > 1024 for x in workers):
        parser.error('use positive simulations/repetitions and worker counts from 1 to 1024')
    if args.min_length <= 0 or not 0 <= args.seed < 1 << 64:
        parser.error('use positive minimum length and a seed from 0 to 2^64-1')
    inputs = (args.baseline, args.candidate, args.dice, args.dictionary)
    if args.json and args.json.resolve() in {p.resolve() for p in inputs}:
        parser.error('JSON output must not overwrite an input file')

    report = {
        'platform': platform.platform(),
        'arguments': {key: str(value) if isinstance(value, pathlib.Path) else value
                      for key, value in vars(args).items()},
        'input_sha256': {str(p): sha256(p) for p in inputs},
        'trials': [],
        'summaries': [],
    }
    binaries = [('baseline', args.baseline.resolve()), ('candidate', args.candidate.resolve())]
    expected = None
    order_rng = random.Random(31971)

    def save():
        if args.json:
            args.json.write_text(json.dumps(report, indent=2) + '\n')

    with tempfile.TemporaryDirectory() as tmp:
        csv = pathlib.Path(tmp) / 'frequencies.csv'
        print('version,workers,trial,simulation_seconds', flush=True)
        for threads in workers:
            for trial in range(args.repeat):
                order = binaries[:]
                order_rng.shuffle(order)
                for name, binary in order:
                    cmd = [str(binary), str(args.dice), str(args.dictionary),
                           '--sims', str(args.sims), '--threads', str(threads),
                           '--seed', str(args.seed), '--min-length', str(args.min_length),
                           '--csv', str(csv)]
                    proc = subprocess.run(cmd, capture_output=True, check=True)
                    timing = re.search(rb'Simulated \d+ boards on \d+ worker\(s\) in ([0-9.e+-]+) s', proc.stderr)
                    if not timing:
                        raise RuntimeError('Cannot parse simulation timing: ' + proc.stderr.decode())
                    hashes = (hashlib.sha256(proc.stdout).hexdigest(), sha256(csv))
                    if expected is None:
                        expected = hashes
                    if hashes != expected:
                        raise RuntimeError(f'{name}, {threads} workers: ranking or full CSV differs')
                    seconds = float(timing[1])
                    report['trials'].append(dict(version=name, workers=threads, trial=trial + 1,
                        seconds=seconds, summary_sha256=hashes[0], csv_sha256=hashes[1]))
                    save()
                    print(f'{name},{threads},{trial + 1},{seconds:.6f}', flush=True)

    print('\nworkers,baseline_median,candidate_median,throughput_ratio')
    for threads in workers:
        times = {name: [r['seconds'] for r in report['trials']
                       if r['workers'] == threads and r['version'] == name]
                 for name, _ in binaries}
        base = statistics.median(times['baseline'])
        candidate = statistics.median(times['candidate'])
        report['summaries'].append(dict(workers=threads, baseline_median=base,
            candidate_median=candidate, throughput_ratio=base / candidate,
            baseline_range=[min(times['baseline']), max(times['baseline'])],
            candidate_range=[min(times['candidate']), max(times['candidate'])]))
        print(f'{threads},{base:.6f},{candidate:.6f},{base / candidate:.3f}')
    print('All rankings and full CSV frequencies are byte-identical across versions and worker counts.')
    save()


if __name__ == '__main__':
    main()
