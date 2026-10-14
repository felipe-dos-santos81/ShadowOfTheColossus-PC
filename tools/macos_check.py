#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The macOS acceptance gate: run the port and check the log for what "it runs" means.

    python3 tools/macos_check.py [--seconds N]

Exit 0 when the run is acceptable, 1 with the problems it found. The markers tell "the port
runs" apart from "the port did not": the image below 2 GiB, a windowed window with the cursor
free, a Vulkan presenter, and a chosen BPE heap. A STOP:, an assertion failure, or a Python
traceback is a failure even when every marker appears.
"""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
from typing import NamedTuple

PORT = Path(__file__).resolve().parent.parent


class Run(NamedTuple):
    """A captured run: the problems found in it, and the log they were found in."""

    problems: list[str]
    log: str


MARKERS = (
    ('image below 2 GiB', r'Image at 0x[0-9a-f]+ \(below 2 GiB'),
    ('windowed, unlocked cursor', r'Window \d+x\d+ windowed: relative mouse off, grab off'),
    ('presenter ready', r'window and Vulkan presenter ready'),
    ('vulkan driver named', r'GPU_Vulkan_Driver: \S+'),
    ('heap guard', r'BPE heap guard on heap'),
)

FORBIDDEN = (
    ('stopped', r'STOP:'),
    ('assertion', r'[Aa]ssertion [Ff]ailed'),
    ('python traceback', r'Traceback \(most recent call last\)'),
)


def check_log(log, returncode=None):
    """The problems a captured log has, in marker order; empty when the run is acceptable.

    `returncode` is the process's status when it stopped on its own before the gate's deadline
    (None when the gate ended it, or when it exited cleanly): a crash after every marker appeared
    is still a failure.
    """
    problems = [f'missing: {name}' for name, pattern in MARKERS if not re.search(pattern, log)]
    if returncode is not None:
        problems.append(f'forbidden: exited before the deadline (rc {returncode})')
    problems += [f'forbidden: {name}' for name, pattern in FORBIDDEN if re.search(pattern, log)]
    return problems


def run(port, seconds=30.0, command=None):
    """Run the port for `seconds`, end its process group, and return a `Run`.

    The output is drained on a thread so a process that stops printing cannot block the deadline;
    the process group gets SIGTERM and, if it is ignored, SIGKILL (a game that never closes its
    window must not hang the gate). The gate raises the GPU log to `info` so the Vulkan driver it
    tried is named: the port's default threshold is `warning`, which filters that one line.
    """
    child_env = dict(os.environ)
    child_env.setdefault('BB_GPU_LOG', 'info')
    process = subprocess.Popen(command or [sys.executable, 'sotc.py'], cwd=port, env=child_env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    lines = []

    def drain():
        try:
            lines.extend(process.stdout)
        except (ValueError, OSError):
            pass  # the reader raced the close below; whatever it captured is already in `lines`

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    deadline = time.monotonic() + seconds
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.05)
    ended_by_gate = process.poll() is None
    if ended_by_gate:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        except ProcessLookupError:
            pass  # the group ended between poll() and the signal
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=5)
    reader.join(timeout=5)
    if not reader.is_alive():
        process.stdout.close()
    log = ''.join(lines)
    # The gate ends the run itself, so a process that stopped on its own is a failure: a clean
    # exit or a signal, after the markers, is not the "still running" a PASS claims.
    returncode = None if ended_by_gate or process.returncode == 0 else process.returncode
    return Run(check_log(log, returncode), log)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--seconds', type=float, default=30.0,
                        help='how long to let the game run (default: 30)')
    args = parser.parse_args(argv)
    result = run(PORT, seconds=args.seconds)
    if not result.problems:
        print('macos_check: PASS')
        return 0
    for problem in result.problems:
        print(problem, file=sys.stderr)
    print('--- captured log ---', file=sys.stderr)
    sys.stderr.write(result.log)
    return 1


if __name__ == '__main__':
    sys.exit(main())
