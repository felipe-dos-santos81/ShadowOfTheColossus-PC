"""extract_pkg must replace a stale real directory, not crash on it, and a missing game must be a
message rather than a traceback."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from paths import ROOT

sys.path.insert(0, str(ROOT))
import sotc


class ExtractPkg(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        # extract_pkg discovers the tool at out/pkgtool/PkgTool.Core; the stub stands in for it.
        self.tool = self.out / 'pkgtool' / 'PkgTool.Core'
        self.tool.parent.mkdir(parents=True)
        # PkgTool.Core's contract: pkg_extract [--passcode ...] <input.pkg> <output_directory>, and
        # pkg_extractentry <input.pkg> <entry_id> <output_file>. Only the first writes a tree, so
        # the stub branches on the subcommand instead of treating every last argument as a directory.
        self.tool.write_text('#!/bin/sh\n'
                             'if [ "$1" = pkg_extract ]; then\n'
                             '  eval "out=\\${$#}"\n'
                             '  mkdir -p "$out/uroot" "$out/uroot/sce_sys"\n'
                             '  printf x > "$out/uroot/eboot.bin"\n'
                             '  printf x > "$out/uroot/sce_sys/param.sfo"\n'
                             'fi\n')
        self.tool.chmod(0o755)

    def test_replaces_a_stale_real_directory(self):
        stale = self.out / 'CUSA08809'
        stale.mkdir()
        (stale / 'junk').write_text('left over')       # no eboot.bin: a partial extract
        target = sotc.extract_pkg(self.out / 'game.pkg', self.out)
        self.assertTrue((target / 'eboot.bin').is_file())
        self.assertFalse((target / 'junk').exists())

    def test_keeps_an_existing_complete_extract(self):
        good = self.out / 'CUSA08809'
        (good / 'sce_sys').mkdir(parents=True)
        (good / 'eboot.bin').write_text('x')
        (good / 'sce_sys' / 'param.sfo').write_text('x')
        self.assertEqual(sotc.extract_pkg(self.out / 'game.pkg', self.out), good)

    def test_replaces_a_stale_regular_file(self):
        (self.out / 'CUSA08809').write_text('a leftover file, not a directory')
        target = sotc.extract_pkg(self.out / 'game.pkg', self.out)
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
