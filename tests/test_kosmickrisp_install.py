"""tools/install_kosmickrisp.sh owns the KosmicKrisp layout contract: the three files, an x86-64
driver slice, and a clear failure otherwise; tools/package_kosmickrisp.sh ships through it. Driven
with fixture drivers, so no Mesa build."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from paths import ROOT

INSTALLER = ROOT / 'tools' / 'install_kosmickrisp.sh'
PACKAGER = ROOT / 'tools' / 'package_kosmickrisp.sh'
FILES = ('libvulkan_kosmickrisp.dylib', 'libz.1.dylib', 'kosmickrisp_mesa_icd.json')
# A Mach-O that every macOS install has, with both slices: a real driver fixture for the lipo check.
SYSTEM_BINARY = Path('/usr/bin/true')


def macho(slice_name, dest):
    """A Mach-O holding only `slice_name` of the system binary; None when there is no such slice."""
    done = subprocess.run(['lipo', '-thin', slice_name, '-output', str(dest), str(SYSTEM_BINARY)],
                          capture_output=True, text=True)
    return dest if done.returncode == 0 else None


@unittest.skipUnless(sys.platform == 'darwin', 'the driver tooling is macOS only')
class Fixture(unittest.TestCase):
    """A driver directory built from a thinned system binary and two stand-in files."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.src = self.tmp / 'driver'
        self.dest = self.tmp / 'out'
        self.src.mkdir()
        if macho('x86_64', self.src / FILES[0]) is None:
            self.skipTest('lipo cannot produce an x86_64 slice on this machine')
        self.driver_bytes = (self.src / FILES[0]).read_bytes()
        (self.src / FILES[1]).write_text('not really zlib')
        (self.src / FILES[2]).write_text('{"file_format_version":"1.0.0","ICD":{}}')

    def run_script(self, script, *args):
        return subprocess.run(['bash', str(script), *map(str, args)], capture_output=True, text=True)


class InstallKosmickrisp(Fixture):
    def install(self, source=None):
        return self.run_script(INSTALLER, source or self.src, self.dest)

    def test_installs_the_three_files_from_a_directory(self):
        done = self.install()
        self.assertEqual(done.returncode, 0, done.stderr)
        for name in FILES:
            self.assertTrue((self.dest / name).is_file(), name)
        self.assertEqual((self.dest / FILES[0]).read_bytes(), self.driver_bytes)

    def test_installs_from_a_flat_tarball(self):
        # The layout tools/package_kosmickrisp.sh writes: the three files at the top level.
        tarball = self.tmp / 'driver.tar.gz'
        subprocess.run(['tar', '-czf', str(tarball), '-C', str(self.src), *FILES], check=True)
        done = self.install(tarball)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue((self.dest / FILES[0]).is_file())

    def test_installs_from_a_tarball_with_a_top_level_directory(self):
        nested = self.tmp / 'nested'
        (nested / 'driver').mkdir(parents=True)
        for name in FILES:
            shutil.copy(self.src / name, nested / 'driver' / name)
        tarball = self.tmp / 'nested.tar.gz'
        subprocess.run(['tar', '-czf', str(tarball), '-C', str(nested), 'driver'], check=True)
        done = self.install(tarball)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue((self.dest / FILES[0]).is_file())

    def test_names_the_missing_file(self):
        (self.src / FILES[1]).unlink()
        done = self.install()
        self.assertEqual(done.returncode, 1)
        self.assertIn(FILES[1], done.stderr)

    def test_rejects_a_driver_without_an_x86_64_slice(self):
        if macho('arm64e', self.src / FILES[0]) is None:
            self.skipTest('lipo cannot produce an arm64e slice on this machine')
        done = self.install()
        self.assertEqual(done.returncode, 1)
        self.assertIn('no x86_64 slice', done.stderr)

    def test_rejects_a_file_that_is_not_a_macho(self):
        (self.src / FILES[0]).write_text('not a Mach-O at all')
        done = self.install()
        self.assertEqual(done.returncode, 1)
        self.assertIn('not a Mach-O', done.stderr)

    def test_missing_source_is_a_message(self):
        done = self.install(self.tmp / 'nowhere')
        self.assertEqual(done.returncode, 1)
        self.assertIn('not found', done.stderr)


class PackageKosmickrisp(Fixture):
    """The packager stages through the installer, so it does not restate that contract itself."""

    def package(self):
        return self.run_script(PACKAGER, self.src, self.tmp / 'dist')

    def test_the_packaged_asset_installs_back(self):
        done = self.package()
        self.assertEqual(done.returncode, 0, done.stderr)
        asset = self.tmp / 'dist' / 'kosmickrisp-macos-x86_64.tar.gz'
        self.assertTrue(asset.is_file())
        self.assertTrue((asset.parent / (asset.name + '.sha256')).is_file())
        back = self.tmp / 'back'
        landed = self.run_script(INSTALLER, asset, back)
        self.assertEqual(landed.returncode, 0, landed.stderr)
        self.assertEqual((back / FILES[0]).read_bytes(), self.driver_bytes)

    def test_refuses_a_driver_without_an_x86_64_slice(self):
        if macho('arm64e', self.src / FILES[0]) is None:
            self.skipTest('lipo cannot produce an arm64e slice on this machine')
        done = self.package()
        self.assertEqual(done.returncode, 1)
        self.assertIn('no x86_64 slice', done.stderr)

    def test_says_to_run_make_driver_when_nothing_is_built(self):
        (self.src / FILES[0]).unlink()
        done = self.package()
        self.assertEqual(done.returncode, 1)
        self.assertIn('make driver', done.stderr)


if __name__ == '__main__':
    unittest.main()
