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

PORT = Path(__file__).resolve().parent.parent

MARKERS = (
    ('image below 2 GiB', r'Image at 0x[0-9a-f]+ \(below 2 GiB'),
    ('windowed, unlocked cursor', r'Window \d+x\d+ windowed: relative mouse off, grab off'),
    ('presenter ready', r'window and Vulkan presenter ready'),
    ('heap guard', r'BPE heap guard on heap'),
)

FORBIDDEN = (
    ('stopped', r'STOP:'),
    ('assertion', r'Assertion failed'),
    ('python traceback', r'Traceback \(most recent call last\)'),
)


def check_log(log):
    """The problems a captured log has, in marker order; empty when the run is acceptable."""
    problems = [f'missing: {name}' for name, pattern in MARKERS if not re.search(pattern, log)]
    problems += [f'forbidden: {name}' for name, pattern in FORBIDDEN if re.search(pattern, log)]
    return problems


def run(port, seconds=30.0, command=None, env=None):
    """Run the port for `seconds`, end its process group, and return (acceptable, log).

    The output is drained on a thread so a process that stops printing cannot block the deadline;
    the process group gets SIGTERM and, if it is ignored, SIGKILL (a game that never closes its
    window must not hang the gate).
    """
    process = subprocess.Popen(command or [sys.executable, 'sotc.py'], cwd=port, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               start_new_session=True)
    lines = []
    reader = threading.Thread(target=lambda: lines.extend(process.stdout), daemon=True)
    reader.start()
    deadline = time.monotonic() + seconds
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.05)
    if process.poll() is None:
        os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
            process.wait(timeout=5)
    reader.join(timeout=5)
    process.stdout.close()
    log = ''.join(lines)
    return (not check_log(log), log)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--seconds', type=float, default=30.0,
                        help='how long to let the game run (default: 30)')
    args = parser.parse_args(argv)
    ok, log = run(PORT, seconds=args.seconds)
    if ok:
        print('macos_check: PASS')
        return 0
    for problem in check_log(log):
        print(problem, file=sys.stderr)
    print('--- captured log ---', file=sys.stderr)
    sys.stderr.write(log)
    return 1


if __name__ == '__main__':
    sys.exit(main())
