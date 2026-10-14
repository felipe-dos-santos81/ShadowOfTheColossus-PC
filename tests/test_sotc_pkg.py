"""extract_pkg must replace a stale real directory, not crash on it, and a missing game must be a
message rather than a traceback."""
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import sotc


class ExtractPkg(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        self.tool = self.out / 'PkgTool.Core'
        # PkgTool.Core's contract: the output directory is the last argument
        # (pkg_extract [--passcode ...] <input.pkg> <output_directory>).
        self.tool.write_text('#!/bin/sh\n'
                             'eval "out=\\${$#}"\n'
                             'mkdir -p "$out/uroot" "$out/uroot/sce_sys"\n'
                             'printf x > "$out/uroot/eboot.bin"\n'
                             'printf x > "$out/uroot/sce_sys/param.sfo"\n')
        self.tool.chmod(0o755)

    def test_replaces_a_stale_real_directory(self):
        stale = self.out / 'CUSA08809'
        stale.mkdir()
        (stale / 'junk').write_text('left over')       # no eboot.bin: a partial extract
        target = sotc.extract_pkg(self.out / 'game.pkg', self.out, tool=self.tool)
        self.assertTrue((target / 'eboot.bin').is_file())
        self.assertFalse((target / 'junk').exists())

    def test_keeps_an_existing_complete_extract(self):
        good = self.out / 'CUSA08809'
        (good / 'sce_sys').mkdir(parents=True)
        (good / 'eboot.bin').write_text('x')
        (good / 'sce_sys' / 'param.sfo').write_text('x')
        self.assertEqual(sotc.extract_pkg(self.out / 'game.pkg', self.out, tool=self.tool), good)

    def test_replaces_a_stale_regular_file(self):
        (self.out / 'CUSA08809').write_text('a leftover file, not a directory')
        target = sotc.extract_pkg(self.out / 'game.pkg', self.out, tool=self.tool)
        self.assertTrue((target / 'eboot.bin').is_file())


class MissingGame(unittest.TestCase):
    def test_missing_game_is_a_message_not_a_traceback(self):  # Review Focus 2
        env = dict(os.environ, SOTC_GAME_DIR='/nonexistent/sotc-game')
        done = subprocess.run([sys.executable, str(ROOT / 'sotc.py')], cwd=ROOT, env=env,
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn('SOTC_GAME_DIR', done.stderr)
        self.assertNotIn('Traceback', done.stderr)


if __name__ == '__main__':
    unittest.main()
