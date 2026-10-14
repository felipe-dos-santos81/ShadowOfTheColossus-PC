"""build.sh must re-run vcpkg after an interrupted install, and skip a complete one."""
import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / 'tools' / 'vcpkg_ready.sh'


class VcpkgReady(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def ready(self):
        return subprocess.run(['bash', str(SCRIPT), str(self.dir)], capture_output=True, text=True)

    def test_missing_directory_is_not_ready(self):
        self.assertNotEqual(self.ready().returncode, 0)

    def test_partly_installed_is_not_ready(self):
        (self.dir / 'lib').mkdir()
        (self.dir / 'lib' / 'libSDL3.a').write_bytes(b'')
        done = self.ready()
        self.assertNotEqual(done.returncode, 0)
        self.assertIn('libZydis.a', done.stderr)

    def test_complete_install_is_ready(self):
        (self.dir / 'lib').mkdir()
        for name in ('libSDL3.a', 'libZydis.a'):
            (self.dir / 'lib' / name).write_bytes(b'')
        self.assertEqual(self.ready().returncode, 0)


if __name__ == '__main__':
    unittest.main()
