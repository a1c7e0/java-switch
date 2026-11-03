import hashlib
import importlib.util
import io
import json
import pathlib
import subprocess
import tarfile
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'scripts/openjdk25.py'
SPEC = importlib.util.spec_from_file_location('openjdk25_prepare_under_test', SCRIPT)
OPENJDK25 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OPENJDK25)


def archive_with_source(path, files):
    with tarfile.open(path, 'w:gz') as archive:
        for name, content in files.items():
            data = content.encode()
            info = tarfile.TarInfo('openjdk25-source/' + name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


def unified_patch(old, new):
    return ('--- a/sample.txt\n'
            '+++ b/sample.txt\n'
            '@@ -1 +1 @@\n'
            '-' + old + '\n'
            '+' + new + '\n')


def tree_snapshot(root):
    return {
        path.relative_to(root).as_posix():
            (path.is_dir(), None if path.is_dir() else path.read_bytes(), path.stat().st_mtime_ns)
        for path in sorted(root.rglob('*'))
    }


class OpenJDK25PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='openjdk25-prepare-test-')
        self.root = pathlib.Path(self.temporary.name) / 'repo'
        self.deps = self.root / '.deps'
        self.source = self.deps / 'openjdk25-source'
        self.port = self.deps / 'openjdk25-horizon'
        self.patch_dir = self.root / 'patches/openjdk25'
        self.overlay_dir = self.root / 'ports/openjdk25/src'
        self.patch_dir.mkdir(parents=True)
        self.overlay_dir.mkdir(parents=True)
        self.deps.mkdir(exist_ok=True)

        archive = self.deps / 'openjdk25.archive'
        archive_with_source(archive, {
            'sample.txt': 'upstream\n',
            'unpatched.txt': 'upstream file\n',
        })
        entry = {
            'revision': 'fixture-revision',
            'url': 'fixture://openjdk25-source',
            'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        }
        (self.root / 'dependencies.lock.json').write_text(
            json.dumps({'openjdk25_source': entry}))

        self.patch = self.patch_dir / '0001-fixture.patch'
        self.patch.write_text(unified_patch('upstream', 'patched once'))
        self.overlay = self.overlay_dir / 'hotspot/os/horizon/fixture_native.cpp'
        self.overlay.parent.mkdir(parents=True)
        self.overlay.write_text('native overlay fixture\n')

        self.previous_paths = (OPENJDK25.ROOT, OPENJDK25.DEPS, OPENJDK25.SOURCE,
                               OPENJDK25.PORT)
        OPENJDK25.ROOT = self.root
        OPENJDK25.DEPS = self.deps
        OPENJDK25.SOURCE = self.source
        OPENJDK25.PORT = self.port

    def tearDown(self):
        (OPENJDK25.ROOT, OPENJDK25.DEPS, OPENJDK25.SOURCE,
         OPENJDK25.PORT) = self.previous_paths
        self.temporary.cleanup()

    def test_failed_patch_preserves_published_tree_and_corrected_retry_succeeds(self):
        OPENJDK25.prepare()
        sample = self.port / 'sample.txt'
        overlay = self.port / 'src/hotspot/os/horizon/fixture_native.cpp'
        self.assertEqual(sample.read_text(), 'patched once\n')
        self.assertEqual(overlay.read_text(), 'native overlay fixture\n')
        self.assertTrue((self.port / '.javaswitch-patches').is_file())
        self.assertTrue((self.port / '.javaswitch-source-patches').is_file())
        self.assertTrue((self.port / '.javaswitch-overlay-files.json').is_file())
        overlay_mtime = overlay.stat().st_mtime_ns
        published = tree_snapshot(self.port)

        self.patch.write_text(unified_patch('not present in source', 'broken'))
        with self.assertRaises(subprocess.CalledProcessError):
            OPENJDK25.prepare()
        self.assertEqual(tree_snapshot(self.port), published)
        self.assertEqual(list(self.deps.glob('.openjdk25-prepare-*')), [])

        self.patch.write_text(unified_patch('upstream', 'corrected retry'))
        OPENJDK25.prepare()
        self.assertEqual(sample.read_text(), 'corrected retry\n')
        self.assertEqual(overlay.read_text(), 'native overlay fixture\n')
        self.assertEqual(overlay.stat().st_mtime_ns, overlay_mtime)
        self.assertEqual(list(self.deps.glob('.openjdk25-prepare-*')), [])


if __name__ == '__main__':
    unittest.main()
