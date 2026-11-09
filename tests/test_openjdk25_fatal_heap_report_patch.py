import pathlib
import re
import subprocess
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / '.deps/openjdk25.archive'
PATCH = ROOT / 'patches/openjdk25/0023-horizon-incomplete-heap-error-report.patch'
SOURCE_MEMBER = ('jdk25u-275848b087112429235f4f235822e294a1a288ef/'
                 'src/hotspot/share/utilities/vmError.cpp')


class FatalHeapReportPatchTests(unittest.TestCase):
    @unittest.skipUnless(ARCHIVE.is_file(), 'pinned OpenJDK source archive is not prepared')
    def test_patch_applies_cleanly_and_limits_behavior_change_to_horizon(self):
        with tarfile.open(ARCHIVE, 'r:*') as archive:
            pristine = archive.extractfile(SOURCE_MEMBER).read()

        with tempfile.TemporaryDirectory(prefix='openjdk25-fatal-report-') as temp:
            source = pathlib.Path(temp) / 'src/hotspot/share/utilities/vmError.cpp'
            source.parent.mkdir(parents=True)
            source.write_bytes(pristine)
            subprocess.run(
                ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(PATCH)],
                cwd=temp, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            patched = source.read_text()

        # The non-Horizon report remains byte-for-byte the upstream heap/GC path.
        expected = re.search(
            r'STEP_IF\("printing heap information", _verbose\)\n(.*?)\n  STEP_IF\("printing metaspace information"',
            pristine.decode(), re.S).group(1)
        actual = re.search(
            r'STEP_IF\("printing heap information", _verbose\)\n(.*?)\n  STEP_IF\("printing metaspace information"',
            patched, re.S).group(1)
        heap_part, gc_part = actual.split('STEP_IF("printing GC information", _verbose)', 1)
        horizon_heap, upstream_heap = heap_part.split('#else\n', 1)
        upstream_heap = upstream_heap.rsplit('#endif', 1)[0]
        horizon_gc, upstream_gc = gc_part.split('#ifdef HORIZON\n', 1)[1].split('#else\n', 1)
        upstream_gc = upstream_gc.rsplit('#endif', 1)[0]
        expected_heap, expected_gc = expected.split('STEP_IF("printing GC information", _verbose)', 1)
        expected_gc = expected_gc.split('if (Universe::is_fully_initialized())', 1)[0]
        self.assertEqual(upstream_heap.strip(), expected_heap.strip())
        self.assertEqual(upstream_gc.strip(), expected_gc.strip())

        # Horizon emits an explicit unavailable state and guards each heap virtual call.
        horizon = horizon_heap + horizon_gc
        self.assertIn('if (Universe::is_fully_initialized())', horizon)
        self.assertIn('Heap: (unavailable: Universe is not fully initialized)', horizon)
        self.assertIn('GC information: unavailable (Universe is not fully initialized)', actual)
        self.assertLess(horizon.index('if (Universe::is_fully_initialized())'),
                        horizon.index('Universe::heap()->print_heap_on(st)'))
        gc = actual.split('STEP_IF("printing GC information", _verbose)', 1)[1]
        self.assertIn('Universe::is_fully_initialized()', gc.split('#else', 1)[0])
        self.assertIn('Universe::heap()->print_gc_on(st)', gc)
        self.assertIn('STEP_IF("printing end marker", _verbose)', patched)


if __name__ == '__main__':
    unittest.main()
