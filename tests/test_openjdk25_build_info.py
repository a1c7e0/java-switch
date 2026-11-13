import importlib.util
import pathlib
import subprocess
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'scripts/openjdk25.py'
SPEC = importlib.util.spec_from_file_location('openjdk25_build_info_under_test', SCRIPT)
OPENJDK25 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OPENJDK25)


class OpenJDK25BuildInfoTests(unittest.TestCase):
    def test_refresh_tracks_current_commit_and_dirty_state(self):
        with tempfile.TemporaryDirectory(prefix='openjdk25-build-info-') as temporary:
            root = pathlib.Path(temporary)
            (root / 'scripts').mkdir()
            (root / 'build').mkdir()
            script = pathlib.Path(__file__).resolve().parents[1] / 'scripts/build-info.py'
            (root / 'scripts/build-info.py').write_bytes(script.read_bytes())
            tracked = root / 'tracked.txt'
            tracked.write_text('committed\n')

            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            subprocess.run(['git', 'config', 'user.email', 'build-info-test@example.invalid'],
                           cwd=root, check=True)
            subprocess.run(['git', 'config', 'user.name', 'Build Info Test'], cwd=root, check=True)
            subprocess.run(['git', 'add', 'scripts/build-info.py', 'tracked.txt'],
                           cwd=root, check=True)
            subprocess.run(['git', 'commit', '-qm', 'fixture source'], cwd=root, check=True)
            revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                                cwd=root, text=True).strip()

            previous_root = OPENJDK25.ROOT
            try:
                OPENJDK25.ROOT = root
                OPENJDK25.refresh_build_info()
                header = root / 'build/build-info.h'
                self.assertEqual(header.read_text(),
                                 f'#define JS_BUILD_REVISION "{revision}"\n')

                tracked.write_text('working tree changed\n')
                OPENJDK25.refresh_build_info()
                self.assertEqual(header.read_text(),
                                 f'#define JS_BUILD_REVISION "{revision}-dirty"\n')
            finally:
                OPENJDK25.ROOT = previous_root


if __name__ == '__main__':
    unittest.main()
