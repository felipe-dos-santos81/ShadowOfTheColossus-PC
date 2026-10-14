"""sotc.py's sound-module selection: preset, missing file, and unset all have a stated result."""
import sys
import tempfile
import unittest
from pathlib import Path

from paths import ROOT

sys.path.insert(0, str(ROOT))
import sotc


class SelectSysModules(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)

    def test_unset_runs_silently_and_says_so(self):
        modules, warnings = sotc.select_sys_modules(None)
        self.assertEqual(modules, [])
        self.assertEqual(warnings, ['SOTC_SYS_MODULES not set: no sound (see sotc.py)'])

    def test_complete_folder_links_both_modules(self):
        for name in sotc.SYS_MODULES:
            (self.folder / name).write_bytes(b'')
        modules, warnings = sotc.select_sys_modules(str(self.folder))
        self.assertEqual([p.name for p in modules], list(sotc.SYS_MODULES))
        self.assertEqual(warnings, [])

    def test_missing_file_is_named_and_the_game_still_runs(self):  # Review Focus 5
        (self.folder / 'libSceUlt.sprx').write_bytes(b'')
        modules, warnings = sotc.select_sys_modules(str(self.folder))
        self.assertEqual(modules, [])
        self.assertEqual(len(warnings), 1)
        self.assertIn('libSceNgs2.sprx', warnings[0])
        self.assertNotIn('libSceUlt.sprx,', warnings[0] + ',')


if __name__ == '__main__':
    unittest.main()
