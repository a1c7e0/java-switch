import hashlib
import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'bootstrap_deps', ROOT / 'scripts/bootstrap-deps.py')
BOOTSTRAP_DEPS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP_DEPS)


class BootstrapDependencyTests(unittest.TestCase):
    def test_replaces_shared_minijvm_link_without_touching_source(self):
        source = ROOT / '.deps/minijvm-source'
        patch = ROOT / 'patches/minijvm-horizon.patch'
        header = ROOT / 'src/minijvm_mman.h'
        digest = hashlib.sha256(patch.read_bytes() + header.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix='bootstrap-deps-test-') as directory:
            root = pathlib.Path(directory)
            shared = root / 'shared-minijvm'
            shared.mkdir()
            (shared / 'owner-data').write_text('preserve')
            target = root / 'deps/minijvm'
            target.parent.mkdir()
            target.symlink_to(shared, target_is_directory=True)
            stamp = root / 'deps/minijvm-patch.sha256'
            shared_stamp = root / 'owner-patch-stamp'
            shared_stamp.write_text('owner stamp')
            stamp.symlink_to(shared_stamp)

            BOOTSTRAP_DEPS.prepare_minijvm(source, target, patch, header, stamp, digest)

            self.assertFalse(target.is_symlink())
            self.assertEqual((shared / 'owner-data').read_text(), 'preserve')
            self.assertEqual((target / 'jvm/sc_mman.h').read_bytes(), header.read_bytes())
            self.assertFalse(stamp.is_symlink())
            self.assertEqual(shared_stamp.read_text(), 'owner stamp')
            self.assertEqual(stamp.read_text(), digest)
            self.assertNotEqual(target.resolve(), shared.resolve())


if __name__ == '__main__':
    unittest.main()
