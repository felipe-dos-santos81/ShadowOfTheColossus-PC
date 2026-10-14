"""The macOS acceptance gate's log checker, driven by captured logs (no GPU)."""
import sys
import time
import unittest

from paths import ROOT

sys.path.insert(0, str(ROOT))
from tools import macos_check

GOOD = """
Image at 0x1c900000 (below 2 GiB, as the game's 31-bit pointers need); loader at 0x4ea5d70
GPU [Frontend] <Info> Window 1280x720 windowed: relative mouse off, grab off
GPU [Render.Vulkan] <Info> vk_instance.cpp:740 CollectDeviceParameters: GPU_Vulkan_Driver: KosmicKrisp 26.2.99
GPU: window and Vulkan presenter ready; SDK 0x04508101, 327 HLE symbols
Runtime: BPE heap guard on heap 0x1f6fc700 (2000 of 2000 frees)
"""

# A stub that prints every marker, for the process-control tests (no GPU needed).
MARKER_LINES = (
    'Image at 0x1c900000 (below 2 GiB, pointers)',
    'Window 1280x720 windowed: relative mouse off, grab off',
    'GPU: window and Vulkan presenter ready',
    'GPU_Vulkan_Driver: KosmicKrisp 26.2.99',
    'Runtime: BPE heap guard on heap 0x1f6fc700',
)


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

    def test_reports_the_ports_own_assertion_spelling(self):
        # The port writes "Assertion Failed!" (capital F), so the pattern must not be case-sensitive.
        self.assertEqual(macos_check.check_log(GOOD + 'GPU [Debug] <Critical> buffer.cpp:125 Create: '
                                                    'Assertion Failed!\n'),
                         ['forbidden: assertion'])

    def test_reports_a_traceback(self):
        self.assertEqual(macos_check.check_log(GOOD + 'Traceback (most recent call last):\n'),
                         ['forbidden: python traceback'])

    def test_reports_a_run_that_never_names_the_driver(self):  # Review Focus 3
        without = '\n'.join(line for line in GOOD.splitlines() if 'GPU_Vulkan_Driver' not in line)
        self.assertIn('missing: vulkan driver named', macos_check.check_log(without))

    def test_reports_an_early_exit(self):
        self.assertEqual(macos_check.check_log(GOOD, returncode=-6),
                         ['forbidden: exited before the deadline (rc -6)'])


class RunResult(unittest.TestCase):
    def test_run_returns_problems_and_log(self):  # the result reads, and still unpacks
        problems, log = macos_check.run(ROOT, seconds=1.0,
                                        command=['bash', '-c', 'echo "BPE heap guard on heap x"'])
        self.assertIsInstance(problems, list)
        self.assertIn('BPE heap guard on heap x', log)


class WindowMarkerContract(unittest.TestCase):
    """The window marker is a contract between gpu/shim/window.cpp and macos_check.MARKERS.

    The format alone is not the contract: the gate fails a run whose line says `grab on`, so what
    feeds each `%s` is pinned too. Whitespace is flattened, so reformatting is fine while swapping
    which SDL getter reports which state is not.
    """

    def setUp(self):
        self.source = (ROOT / 'gpu' / 'shim' / 'window.cpp').read_text()
        self.flat = ' '.join(self.source.split())

    def test_the_marker_matches_the_line_window_cpp_prints(self):
        self.assertIn('"Window %dx%d %s: relative mouse %s, grab %s\\n"', self.source)
        pattern = dict(macos_check.MARKERS)['windowed, unlocked cursor']
        line = 'Window %dx%d %s: relative mouse %s, grab %s' % (1280, 720, 'windowed', 'off', 'off')
        self.assertRegex(line, pattern)

    def test_each_state_comes_from_the_getter_that_names_it(self):
        self.assertIn('std::fprintf(stderr, "Window %dx%d %s: relative mouse %s, grab %s\\n", '
                      'width_, height_,', self.flat)
        self.assertIn('(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" '
                      ': "windowed"', self.flat)
        self.assertIn('SDL_GetWindowRelativeMouseMode(window) ? "on" : "off"', self.flat)
        self.assertIn('SDL_GetWindowMouseGrab(window) ? "on" : "off"', self.flat)


class ProcessControl(unittest.TestCase):
    def markers_then(self, tail):
        script = ''.join(f'echo {line!r}\n' for line in MARKER_LINES) + tail + '\n'
        return ['bash', '-c', script]

    def test_run_kills_a_group_that_ignores_sigterm(self):  # Review Focus 4
        start = time.monotonic()
        problems, log = macos_check.run(ROOT, seconds=1.0,
                                        command=['bash', '-c', 'trap "" TERM; echo marker; sleep 30'])
        self.assertTrue(problems)
        self.assertIn('marker', log)
        self.assertLess(time.monotonic() - start, 20)

    def test_run_accepts_a_run_the_gate_itself_ends(self):
        # Every marker, then it keeps running: the gate ends it, which is a pass.
        problems, _ = macos_check.run(ROOT, seconds=1.0, command=self.markers_then('sleep 30'))
        self.assertEqual(problems, [])

    def test_run_fails_a_game_that_dies_after_the_markers(self):  # a crash is not a pass
        problems, _ = macos_check.run(ROOT, seconds=5.0, command=self.markers_then('exit 7'))
        self.assertEqual(problems, ['forbidden: exited before the deadline (rc 7)'])

    def test_run_enables_the_gpu_log_that_names_the_driver(self):  # Review Focus 3
        _, log = macos_check.run(ROOT, seconds=1.0,
                                 command=['bash', '-c', 'echo "BB_GPU_LOG=$BB_GPU_LOG"'])
        self.assertIn('BB_GPU_LOG=info', log)


if __name__ == '__main__':
    unittest.main()
