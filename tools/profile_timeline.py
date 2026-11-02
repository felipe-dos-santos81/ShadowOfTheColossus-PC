#!/usr/bin/env python3
"""macos: report of a SOTC_PROFILE_TIMELINE file (src/probe.c mac_profiler): what the game's job
workers, its update thread and the GPU threads were doing at each ~2 ms sample.

    python3 tools/profile_timeline.py <timeline file> [strip rounds=120]

Worker states come from the guest return addresses of the job system (eboot at 0x400000):
  J  running a job                 S  blocked inside a job (host wait: readback, mutex...)
  L  in the job-queue mutex        I  idle loop (queue empty)
  D  dequeue / bookkeeping         .  other
Update thread: G running, g waiting. GPU threads: upper case running, lower case waiting.
"""
import bisect
import collections
import re
import subprocess
import sys

BASE = 0x700000000000
JOB_CALLS = {0x439a73, 0x439ab6}   # JobSystemScheduler::ExecuteJob -> the job's entry point
LOCK_CALL = 0x4387c8               # DoJobScheduling -> scePthreadMutexLock
IDLE_CALL = 0x4387a8               # DoJobScheduling -> wait for work
DEQUEUE = {0x438860, 0x438ad9, 0x438af8, 0x438b0b}

path = sys.argv[1]
strip = int(sys.argv[2]) if len(sys.argv) > 2 else 120

symbols = []
for line in subprocess.run(['nm', '-n', 'out/gpu/bb-probe'], capture_output=True, text=True).stdout.splitlines():
    parts = line.split(' ', 2)
    if len(parts) == 3 and parts[0] and parts[1] in ('t', 'T'):
        symbols.append((int(parts[0], 16), parts[2]))
demangled = subprocess.run(['c++filt', '-_'], input='\n'.join(n for _, n in symbols), capture_output=True,
                           text=True).stdout.splitlines()
starts = [a for a, _ in symbols]


def host_name(address):
    if BASE <= address < BASE + 0x10000000 and starts:
        i = bisect.bisect_right(starts, address) - 1
        if i >= 0:
            return re.sub(r'\(.*', '', demangled[i])
    return ''


names, rounds = {}, collections.defaultdict(dict)
for line in open(path):
    parts = line.split()
    if parts[0] == 'name':
        names[int(parts[1])] = ' '.join(parts[2:])
    elif parts[0] == 't':
        rnd, us, thread, running = int(parts[1]), int(parts[2]), int(parts[3]), parts[4] == '1'
        frames = [int(x, 16) for x in parts[5:]]
        rounds[rnd][thread] = (us, running, frames)

workers = sorted(t for t, n in names.items() if 'BPE' in n)
update = [t for t, n in names.items() if 'Shadow Game' in n]
gpu = sorted(t for t, n in names.items() if 'shadPS4:Gpu' in n or 'RenderFlip' in n)


def stall_kind(frames):
    syms = ' '.join(host_name(f) for f in frames[1:] if f >= BASE)
    if 'ReadMemory' in syms or 'HandleGuestFault' in syms:
        return 'R'
    if 'mutex' in syms:
        return 'M'
    return 'S'


def worker_state(sample):
    us, running, frames = sample
    stack = set(frames[1:])
    if stack & JOB_CALLS:
        return 'J' if running else stall_kind(frames)
    if LOCK_CALL in stack:
        return 'L' if running else 'l'
    if IDLE_CALL in stack:
        return 'I' if running else 'i'
    if stack & DEQUEUE:
        return 'D'
    return '.' if running else ','


def gpu_state(thread, sample):
    letter = {'shadPS4:GpuCommandProcessor': 'C', 'shadPS4:GpuDraw': 'D', 'shadPS4:GpuRecorder': 'R',
              'RenderFlip': 'F'}.get(names[thread], 'x')
    return letter if sample[1] else letter.lower()


ordered = sorted(rounds)
per_round = []
for rnd in ordered:
    row = rounds[rnd]
    w = ''.join(worker_state(row[t]) if t in row else ' ' for t in workers)
    u = ''.join(('G' if row[t][1] else 'g') if t in row else ' ' for t in update)
    g = ''.join(gpu_state(t, row[t]) if t in row else ' ' for t in gpu)
    per_round.append((rnd, w, u, g, row))

n = len(per_round)
print(f'{n} rounds; workers {[names[t] for t in workers]}; update {[names[t] for t in update]}; gpu {[names[t] for t in gpu]}')
jobs = collections.Counter(sum(c == 'J' for c in w) for _, w, _, _, _ in per_round)
print('rounds by number of workers running a job:', ', '.join(f'{k}: {v} ({100.0 * v / n:.0f}%)' for k, v in sorted(jobs.items())))
busy = collections.Counter(sum(c in 'JRMS' for c in w) for _, w, _, _, _ in per_round)
print('rounds by workers inside a job (running or blocked):', ', '.join(f'{k}: {v} ({100.0 * v / n:.0f}%)' for k, v in sorted(busy.items())))
states = collections.Counter(c for _, w, _, _, _ in per_round for c in w)
print('worker samples by state:', ', '.join(f'{k}: {100.0 * v / sum(states.values()):.1f}%' for k, v in states.most_common()))

idle = [(w, u, g, row) for _, w, u, g, row in per_round if not any(c == 'J' for c in w)]
print(f'\nrounds with no job running: {len(idle)} ({100.0 * len(idle) / n:.0f}%)')
blocked = collections.Counter(''.join(sorted(c for c in w if c in 'RMS')) or '-' for w, _, _, _ in idle)
print('  blocked workers in them:', ', '.join(f'{k}: {v}' for k, v in blocked.most_common(8)))
upd = collections.Counter(u for _, u, _, _ in idle)
print('  update thread:', dict(upd))
gpus = collections.Counter(g for _, _, g, _ in idle)
print('  gpu threads (C=cmd proc, D=draw, R=recorder, F=flip; lower=waiting):', ', '.join(f'{k}: {v}' for k, v in gpus.most_common(8)))
# where the update thread waits while nothing runs
where = collections.Counter()
for _, u, _, row in idle:
    for t in update:
        if t in row and not row[t][1]:
            syms = [host_name(f) for f in row[t][2] if f >= BASE]
            key = next((s for s in syms if s and 'pthread' not in s and 'cond' not in s.lower()), syms[0] if syms else '?')
            where[key[:60]] += 1
print('  update thread waits in:', ', '.join(f'{k}: {v}' for k, v in where.most_common(6)))
# the blocked workers' host frames
where = collections.Counter()
for w, _, _, row in idle:
    for t, c in zip(workers, w):
        if c in 'RMS':
            syms = [host_name(f) for f in row[t][2] if f >= BASE]
            key = ' < '.join(s[:28] for s in syms[:4])
            where[key] += 1
print('  blocked workers\' host stacks:', *[f'\n    {v:5d}  {k}' for k, v in where.most_common(8)])

# streaks of no job running
streaks, run = [], 0
for _, w, _, _, _ in per_round:
    if any(c == 'J' for c in w):
        if run: streaks.append(run)
        run = 0
    else:
        run += 1
if streaks:
    streaks.sort()
    print(f'\nno-job streaks: {len(streaks)}, median {streaks[len(streaks) // 2]} rounds, max {streaks[-1]} rounds '
          f'(a round is ~2 ms); rounds in streaks >= 3: {sum(s for s in streaks if s >= 3)}')

print(f'\nstrip of the first {strip} rounds (one line per round: workers | update | gpu threads):')
for rnd, w, u, g, _ in per_round[:strip]:
    print(f'{rnd:6d} {w} | {u} | {g}')
