#!/usr/bin/env python3
import csv
import math
import sys
from collections import defaultdict

def nearest_rank(values, percentile):
    if not values:
        return 0.0
    values = sorted(values)
    index = math.ceil(percentile / 100.0 * len(values)) - 1
    index = max(0, min(index, len(values) - 1))
    return values[index]

def load_rows(path, exclude_seed=True):
    rows = list(csv.DictReader(open(path, newline='')))
    if not exclude_seed:
        return rows
    seeded = set()
    result = []
    for row in rows:
        if row['op'] == 'PUT' and row['filename'] not in seeded:
            seeded.add(row['filename'])
            continue
        result.append(row)
    return result

def main(path):
    rows = load_rows(path, exclude_seed=True)
    waiting = []
    finishes = []
    arrivals = []
    slowdown = defaultdict(list)
    forfeited = defaultdict(int)
    for r in rows:
        b = int(r['bytes'])
        arr = int(r['arrival_ns'])
        start = int(r['start_ns'])
        finish = int(r['finish_ns'])
        waiting.append(start - arr)
        finishes.append(finish)
        arrivals.append(arr)
        if b <= 1024:
            size_class = 'small'
        elif b <= 32768:
            size_class = 'medium'
        else:
            size_class = 'large'
        slowdown[size_class].append((finish - arr) / b if b else 0.0)
        forfeited[size_class] += int(r['forfeited_bytes'])
    n = len(rows)
    throughput = n / ((max(finishes) - min(arrivals)) / 1e9) if n and max(finishes) > min(arrivals) else 0.0
    print(f'reported_requests={n}')
    print(f'waiting_p50_ns={nearest_rank(waiting,50):.0f}')
    print(f'waiting_p99_ns={nearest_rank(waiting,99):.0f}')
    print(f'throughput_requests_per_s={throughput:.6f}')
    long_slow = []
    long_forfeited = 0
    for r in rows:
        if r['filename'] == 'large_long.txt':
            b = int(r['bytes']); arr = int(r['arrival_ns']); fin = int(r['finish_ns'])
            if b: long_slow.append((fin-arr)/b)
            long_forfeited += int(r['forfeited_bytes'])
    if long_slow:
        print(f'large_long_slowdown_median_ns_per_byte={nearest_rank(long_slow,50):.6f}')
        print(f'large_long_slowdown_p99_ns_per_byte={nearest_rank(long_slow,99):.6f}')
        print(f'large_long_forfeited_bytes={long_forfeited}')
    for cls in ('small','medium','large'):
        if slowdown[cls]:
            print(f'{cls}_slowdown_median_ns_per_byte={nearest_rank(slowdown[cls],50):.6f}')
            print(f'{cls}_slowdown_p99_ns_per_byte={nearest_rank(slowdown[cls],99):.6f}')
            print(f'{cls}_forfeited_bytes={forfeited[cls]}')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('usage: python3 analyze_metrics.py <metrics.csv>')
        raise SystemExit(2)
    main(sys.argv[1])
