import pathlib
import shutil
import subprocess
import tempfile
import unittest


REPO = pathlib.Path(__file__).resolve().parents[1]
HOST_JDK_ROOT = REPO / '.deps/host-jdk25'
HOST_JDKS = sorted(HOST_JDK_ROOT.glob('jdk-*'))
if not HOST_JDKS:
    raise RuntimeError('the pinned OpenJDK 25 host JDK must be installed for this test')
HOST_JDK = HOST_JDKS[0]
FFM_FIXTURES = REPO / 'ports/openjdk25/tests/ffm_platform'


class OpenJDK25FfmHostTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='openjdk25-ffm-host-')
        self.addCleanup(self.temporary.cleanup)
        self.base = pathlib.Path(self.temporary.name)

    def make_repo(self, name):
        root = self.base / name
        (root / 'ports/openjdk25/tests').mkdir(parents=True)
        (root / '.deps').mkdir()
        shutil.copytree(FFM_FIXTURES, root / 'ports/openjdk25/tests/ffm_platform')
        (root / '.deps/openjdk25-source').symlink_to(
            REPO / '.deps/openjdk25-source', target_is_directory=True)
        (root / 'patches').symlink_to(REPO / 'patches', target_is_directory=True)
        return root

    def run_host_test(self, root):
        return subprocess.run(
            ['bash', str(root / 'ports/openjdk25/tests/ffm_platform/test-host.sh')],
            text=True, capture_output=True, timeout=120)

    def test_actual_pinned_jdk_direct_and_symlinked_roots(self):
        direct = self.make_repo('direct')
        direct_jdk = direct / '.deps/host-jdk25' / HOST_JDK.name
        direct_jdk.mkdir(parents=True)
        shutil.copytree(HOST_JDK / 'bin', direct_jdk / 'bin')
        for entry in HOST_JDK.iterdir():
            if entry.name == 'bin':
                continue
            (direct_jdk / entry.name).symlink_to(entry,
                                                  target_is_directory=entry.is_dir())
        result = self.run_host_test(direct)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Horizon C ABI initialization host tests passed', result.stdout)

        symlinked = self.make_repo('symlinked')
        (symlinked / '.deps/host-jdk25').symlink_to(
            REPO / '.deps/host-jdk25', target_is_directory=True)
        result = self.run_host_test(symlinked)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Horizon C ABI initialization host tests passed', result.stdout)

    def test_missing_and_empty_jdk_roots_fail_with_clear_error(self):
        missing = self.make_repo('missing')
        result = self.run_host_test(missing)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OpenJDK 25 host javac is required', result.stderr)
        self.assertNotIn('No such file or directory', result.stderr)

        empty = self.make_repo('empty')
        (empty / '.deps/host-jdk25').mkdir()
        result = self.run_host_test(empty)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OpenJDK 25 host javac is required', result.stderr)


if __name__ == '__main__':
    unittest.main()
