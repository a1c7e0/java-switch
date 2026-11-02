import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativeResolverTests(unittest.TestCase):
    def test_real_function_lookup_and_lifetime_gates(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'native-resolver-test'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Iinclude', 'src/native_resolver.c',
                            'tests/native_resolver_host.c', '-o', str(binary)],
                           cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=10)
