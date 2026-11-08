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
                '-Itests/ffi_stubs', '-Iinclude', 'src/horizon_alias.c', 'src/vmem_horizon.c',
                'tests/vmem_horizon_host.c', '-pthread', '-Wl,--wrap=calloc', '-o', str(binary)],
                cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                text=True, capture_output=True)
            self.assertIn('vmem state and injected failures: PASS', result.stdout)

    def test_openjdk_bridge_uses_alias_region(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'horizon-memory-test'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Itests/ffi_stubs', '-Iports/openjdk25/src/hotspot/os/horizon',
                '-Iinclude',
                'src/horizon_alias.c', 'src/vmem_horizon.c',
                'ports/openjdk25/src/hotspot/os/horizon/horizon_memory_bridge.c',
                'tests/horizon_memory_bridge_host.c', '-pthread', '-o', str(binary)],
                cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True,
                text=True, capture_output=True)
            self.assertIn('eager heap-backed bridge paths: PASS', result.stdout)
