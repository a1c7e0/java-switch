import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class HorizonMemoryTests(unittest.TestCase):
    def test_state_and_injected_system_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'vmem-test'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Itests/ffi_stubs', '-Iinclude', 'src/vmem_horizon.c',
                'tests/vmem_horizon_host.c', '-pthread', '-Wl,--wrap=calloc', '-o', str(binary)],
                cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                text=True, capture_output=True)
            self.assertIn('vmem state and injected failures: PASS', result.stdout)
