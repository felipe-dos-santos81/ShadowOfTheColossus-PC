"""The macOS acceptance gate's log checker, driven by captured logs (no GPU)."""
import sys
from pathlib import Path
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import macos_check

GOOD = """
Image at 0x1c900000 (below 2 GiB, as the game's 31-bit pointers need); loader at 0x4ea5d70
GPU [Frontend] <Info> Window 1280x720 windowed: relative mouse off, grab off
GPU: window and Vulkan presenter ready; SDK 0x04508101, 327 HLE symbols
Runtime: BPE heap guard on heap 0x1f6fc700 (2000 of 2000 frees)
"""


class CheckLog(unittest.TestCase):
    def test_accepts_a_complete_run(self):
        self.assertEqual(macos_check.check_log(GOOD), [])

    def test_accepts_a_scaled_window(self):
        # Review Focus 1: a smaller display scales the window; the marker must not fix a size.
        scaled = GOOD.replace('1280x720', '1152x648')
        self.assertEqual(macos_check.check_log(scaled), [])

    def test_reports_the_missing_window_line(self):
        self.assertEqual(macos_check.check_log(GOOD.replace('grab off', 'grab on')),
                         ['missing: windowed, unlocked cursor'])

    def test_reports_each_missing_marker(self):
        problems = macos_check.check_log('nothing useful here\n')
        self.assertEqual(problems, [f'missing: {name}' for name, _ in macos_check.MARKERS])

    def test_reports_a_forbidden_line(self):
        self.assertEqual(macos_check.check_log(GOOD + 'STOP: cannot reserve the guest range\n'),
                         ['forbidden: stopped'])

    def test_reports_a_traceback(self):
        self.assertEqual(macos_check.check_log(GOOD + 'Traceback (most recent call last):\n'),
                         ['forbidden: python traceback'])

    def test_run_kills_a_group_that_ignores_sigterm(self):  # Review Focus 4
        start = time.monotonic()
        ok, log = macos_check.run(ROOT, seconds=1.0,
                                  command=['bash', '-c', 'trap "" TERM; echo marker; sleep 30'])
        self.assertFalse(ok)
        self.assertIn('marker', log)
        self.assertLess(time.monotonic() - start, 20)


if __name__ == '__main__':
    unittest.main()
