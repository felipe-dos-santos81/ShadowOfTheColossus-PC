"""The SotC Tk launcher's platform branches, without a display: the game command, the process
group used by Stop, the settings folder, windowed mode, and unlocked mouse cursor defaults."""
import importlib
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'launcher'))
launcher = importlib.import_module('sotc_launcher_win')


@unittest.skipIf(sys.platform == 'win32', 'POSIX branches')
class PosixLauncher(unittest.TestCase):
    def test_run_command_is_sotc_py(self):
        command = launcher.run_command()
        self.assertEqual(command[0], sys.executable)
        self.assertEqual(Path(command[1]), ROOT / 'sotc.py')
        self.assertIsInstance(command, list)

    def test_windowed_and_unlocked_mouse_defaults(self):
        self.assertFalse(launcher.APP_DEFAULTS['fullscreen'])
        self.assertFalse(launcher.APP_DEFAULTS['mouse'])
        env = launcher.game_environment(dict(launcher.APP_DEFAULTS, game_dir='/tmp/game'))
        self.assertEqual(env['BB_FULLSCREEN'], '0')
        self.assertEqual(env['SOTC_MOUSE'], '0')

    def test_stop_ends_the_whole_process_group(self):
        process = subprocess.Popen(['bash', '-c', 'sleep 30 & echo $! ; wait'], stdout=subprocess.PIPE,
                                   start_new_session=True)
        child = int(process.stdout.readline())
        process.stdout.close()
        job = launcher.GameJob(process)
        job.terminate()
        process.wait(timeout=5)
        for _ in range(50):
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                break
            time.sleep(0.1)
        else:
            self.fail('the grandchild sleep survived Stop')
        job.close()

    def test_stop_kills_a_game_that_ignores_sigterm(self):
        process = subprocess.Popen(['bash', '-c', 'trap "" TERM; sleep 30 & echo $! ; wait'],
                                   stdout=subprocess.PIPE, start_new_session=True)
        child = int(process.stdout.readline())
        process.stdout.close()
        job = launcher.GameJob(process, kill_after=1.0)
        job.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, 9)
            self.fail('the SIGTERM-ignoring game survived Stop')
        for _ in range(50):
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                break
            time.sleep(0.1)
        else:
            os.kill(child, 9)
            self.fail('the grandchild sleep survived Stop')
        job.close()

    def test_config_dir_is_per_platform(self):
        text = str(launcher.CONFIG_DIR)
        if sys.platform == 'darwin':
            self.assertIn('Library/Application Support/sotc-launcher', text)
        else:
            self.assertTrue(text.endswith('sotc-launcher'))

    def test_language_fallback(self):
        self.assertIn(launcher.system_language(), ('pt', 'en'))


if __name__ == '__main__':
    unittest.main()
