"""Filesystem regression for clean configuration; external tools are mocked.

The complete target build is tested separately. These tests run the real helper
control flow up to configure to catch failures hidden by an existing build tree.
"""
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'scripts/openjdk25.py'
SPEC = importlib.util.spec_from_file_location('openjdk25_configure_under_test', SCRIPT)
OPENJDK25 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OPENJDK25)


class OpenJDK25ConfigureTests(unittest.TestCase):
    def test_clean_base_and_instrument_configuration_creates_parent_directories(self):
        for action in ('configure', 'configure-agent'):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                deps = root / '.deps'
                build = root / 'build/openjdk25-horizon'
                port = deps / 'openjdk25-horizon'
                jdk = deps / 'host-jdk25/pinned-jdk/bin'
                jdk.mkdir(parents=True)
                (jdk / 'javac').touch()
                (root / 'build/sdk').mkdir(parents=True)
                ffi_bytes = b'archive fixture consumed by mocked target archiver'
                (root / 'build/libffi-horizon.a').write_bytes(ffi_bytes)
                (root / 'build/sdk/ffi_horizon.o').write_bytes(b'object fixture')
                self.assertFalse(build.exists())

                def configure(command, environment, log):
                    # run() used to create BUILD only after the earlier mkdir
                    # had already failed. Check the real pre-configure files.
                    self.assertTrue(build.is_dir())
                    self.assertEqual((build / 'horizon-libffi/libffi.a').read_bytes(), ffi_bytes)
                    self.assertEqual(command[:2], ['bash', str(port / 'configure')])
                    self.assertIn('--openjdk-target=aarch64-none-horizon', command)
                    prefix = '--enable-' if action == 'configure-agent' else '--disable-'
                    self.assertIn(prefix + 'jvm-feature-jvmti', command)
                    self.assertIn(prefix + 'jvm-feature-services', command)
                    self.assertEqual(log, 'configure.log')
                    return 0

                with mock.patch.multiple(OPENJDK25, ROOT=root, DEPS=deps, PORT=port, BUILD=build), \
                        mock.patch.object(OPENJDK25, 'prepare'), \
                        mock.patch.object(OPENJDK25, 'run', side_effect=configure) as executor, \
                        mock.patch.object(OPENJDK25.subprocess, 'run',
                                          return_value=subprocess.CompletedProcess([], 0)) as archiver, \
                        mock.patch.dict(os.environ, {'DEVKITPRO': str(root / 'devkitpro')}), \
                        mock.patch.object(sys, 'argv', [str(SCRIPT), action]):
                    self.assertEqual(OPENJDK25.work(), 0)
                    self.assertEqual(executor.call_count, 1)
                    self.assertEqual(archiver.call_count, 1)
                    # Existing outputs must be safe to configure again.
                    sentinel = build / 'retained-output'
                    sentinel.write_text('keep')
                    self.assertEqual(OPENJDK25.work(), 0)
                    self.assertEqual(sentinel.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
