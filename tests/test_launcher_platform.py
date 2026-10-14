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

    def test_game_job_close_cancels_kill_timer(self):
        process = subprocess.Popen(['bash', '-c', 'sleep 30'], start_new_session=True)
        job = launcher.GameJob(process, kill_after=30.0)
        job.terminate()
        process.wait(timeout=5)
        timer = job.timer
        self.assertIsNotNone(timer)
        self.assertTrue(timer.daemon)
        self.assertTrue(timer.is_alive())   # the SIGKILL escalation is still pending
        job.close()
        timer.join(timeout=5)               # cancel() lets the thread finish
        self.assertFalse(timer.is_alive())

    def test_run_sh_delegates_to_sotc_py_when_bb_game_dir_unset(self):
        env = dict(os.environ, SOTC_GAME_DIR='/nonexistent/sotc-game')
        env.pop('BB_GAME_DIR', None)
        res = subprocess.run(['bash', str(ROOT / 'run.sh')], cwd=ROOT, env=env,
                             capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn('SOTC_GAME_DIR', res.stderr)

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

    def test_wheel_steps_handles_macos_and_windows_deltas(self):
        # Tk reports 120 per notch on Windows/X11 and 1 per notch on macOS (Aqua). The plain
        # int(-delta / 120) this replaced truncated every macOS notch to 0: no scrolling at all.
        self.assertEqual(int(-1 / 120), 0)
        self.assertEqual(launcher.wheel_steps(120), -1)
        self.assertEqual(launcher.wheel_steps(-120), 1)
        self.assertEqual(launcher.wheel_steps(240), -2)
        self.assertEqual(launcher.wheel_steps(1), -1)
        self.assertEqual(launcher.wheel_steps(-1), 1)
        self.assertEqual(launcher.wheel_steps(0), 0)

    def test_config_dir_is_per_platform(self):
        text = str(launcher.CONFIG_DIR)
        if sys.platform == 'darwin':
            self.assertIn('Library/Application Support/sotc-launcher', text)
        else:
            self.assertTrue(text.endswith('sotc-launcher'))

    def test_language_fallback(self):
        self.assertIn(launcher.system_language(), ('pt', 'en'))

    def test_mouse_defaults_off_when_unset(self):
        # game_environment indexes its settings directly, so the real caller always passes
        # APP_DEFAULTS; the finding is only that 'mouse' defaults to True there.
        settings = {key: value for key, value in launcher.APP_DEFAULTS.items() if key != 'mouse'}
        settings['game_dir'] = '/tmp/game'
        self.assertEqual(launcher.game_environment(settings)['SOTC_MOUSE'], '0')

    def test_save_roots_include_macos_shadps4(self):
        self.assertIn(Path.home() / 'Library' / 'Application Support',
                      launcher.shadps4_saves_roots())

    def test_sidebar_label_is_not_windows_on_posix(self):
        self.assertNotIn('Windows', launcher.platform_label())


if __name__ == '__main__':
    unittest.main()
