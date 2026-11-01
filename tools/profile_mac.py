#!/usr/bin/env python3
"""macos: report of a SOTC_PROFILE=* run (SOTC_PROFILE_OUT file, src/probe.c mac_profiler).

    python3 tools/profile_mac.py <profile file> [threads=6] [frames=25] [name filter]

Threads by samples (running; "(waiting)" entries are the waiting samples, one round in ten),
optionally only those whose name contains the filter, then per thread the hottest functions, inclusive
and self. bb-probe's addresses go through its symbol table (nm out/gpu/bb-probe); other libraries
are named by the game (dladdr); guest code (eboot and its PRXs, below 0x80000000) is shown as guest+offset.
"""
import bisect
import collections
import re
import subprocess
import sys

BASE = 0x700000000000
path = sys.argv[1]
top_threads = int(sys.argv[2]) if len(sys.argv) > 2 else 6
top_frames = int(sys.argv[3]) if len(sys.argv) > 3 else 25
name_filter = sys.argv[4] if len(sys.argv) > 4 else ''
threads, rows = {}, []
libraries = {}  # "lib <address> <library>!<symbol>" lines: names mac_profiler resolved with dladdr
for line in open(path):
    parts = line.split()
    if parts[0] == 'thread':
        threads[int(parts[1])] = (int(parts[2]), float(parts[3]), float(parts[4]), ' '.join(parts[5:]))
    elif parts[0] == 'lib':
        libraries[int(parts[1], 16)] = ' '.join(parts[2:])[:110]
    else:
        rows.append((int(parts[0]), int(parts[1], 16), int(parts[2]), int(parts[3])))

# bb-probe's symbol table (ThinLTO leaves no debug map for atos), demangled by c++filt.
symbols = []
for line in subprocess.run(['nm', '-n', 'out/gpu/bb-probe'], capture_output=True, text=True).stdout.splitlines():
    parts = line.split(' ', 2)
    if len(parts) == 3 and parts[0] and parts[1] in ('t', 'T'):
        symbols.append((int(parts[0], 16), parts[2]))
demangled = subprocess.run(['c++filt', '-_'], input='\n'.join(n for _, n in symbols), capture_output=True,
                           text=True).stdout.splitlines()
starts = [a for a, _ in symbols]


def name(address):
    if address in libraries:
        return libraries[address]
    if BASE <= address < BASE + 0x10000000 and starts:
        i = bisect.bisect_right(starts, address) - 1
        if i >= 0:
            return re.sub(r'\(.*', '', demangled[i])[:110]
    if address < 0x80000000:
        return 'guest'
    return 'other'


busiest = sorted((kv for kv in threads.items() if name_filter in kv[1][3]), key=lambda kv: -kv[1][0])[:top_threads]
for index, (busy, system, user, label) in busiest:
    print(f'{busy:6d} busy  system {system:6.0f} ms  user {user:6.0f} ms  {label}')
for index, (busy, system, user, label) in busiest:
    if not busy:
        continue
    inclusive, leaf = collections.Counter(), collections.Counter()
    for thread, address, l, inc in rows:
        if thread == index:
            n = name(address)
            leaf[n] += l
            inclusive[n] += inc
    print(f'\n== {label} ({busy} busy)')
    print('  self:')
    for n, c in leaf.most_common(top_frames):
        print(f'    {100 * c / busy:5.1f}% {n}')
    print('  inclusive:')
    for n, c in inclusive.most_common(top_frames):
        print(f'    {100 * c / busy:5.1f}% {n}')
