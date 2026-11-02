import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class HorizonClosureTests(unittest.TestCase):
    def test_lifecycle_with_simulated_jit_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'ffi-horizon-test'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Itests/ffi_stubs', '-Ibuild/libffi/include',
                'src/ffi_horizon.c', 'tests/ffi_horizon_host.c',
                '-pthread', '-o', str(binary)], cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                text=True, capture_output=True)
            self.assertIn('lifecycle and injected failures: PASS', result.stdout)
