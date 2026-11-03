import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'scripts/generate-openjdk25-native-resolver.py'


def command(*args, cwd=ROOT, check=True):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, check=check,
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def make_archive(directory, name, source):
    c_path = directory / (name + '.c')
    obj_path = directory / (name + '.o')
    archive = directory / ('lib' + name + '.a')
    c_path.write_text(source)
    command('cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
            '-ffunction-sections', '-fdata-sections', '-c', c_path, '-o', obj_path)
    command('ar', 'rcs', archive, obj_path)
    return archive


class OpenJDK25NativeResolverGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='openjdk25-native-resolver-')
        self.directory = pathlib.Path(self.temp.name)
        self.nm = shutil.which('nm')
        if self.nm is None:
            self.skipTest('nm is required to exercise the static resolver generator')

    def tearDown(self):
        self.temp.cleanup()

    def generate(self, libraries, name='resolver', check=True, symbols=()):
        output = self.directory / (name + '.c')
        manifest = self.directory / (name + '.json')
        args = ['python3', GENERATOR, '--nm', self.nm,
                '--output', output, '--manifest', manifest]
        for module, archive in libraries:
            args.extend(['--library', module + '=' + str(archive)])
        for module, names in symbols:
            args.extend(['--symbols', module + '=' + ','.join(names)])
        result = command(*args, check=check)
        return output, manifest, result

    def test_real_archive_symbols_are_module_and_global_resolvable(self):
        alpha = make_archive(self.directory, 'alpha', r'''
int Java_demo_alpha_value(void) { return 17; }
int native_data = 31;
__attribute__((weak)) int shared_entry(void) { return 11; }
int JVM_probe_entry(void) { return 41; }
int JIMAGE_probe_entry(void) { return 43; }
int JNI_OnLoad_jimage(void *vm, void *reserved) {
    (void)vm; (void)reserved; return 0x00010008;
}
''')
        beta = make_archive(self.directory, 'beta', r'''
int Java_demo_beta_value(void) { return 29; }
int shared_entry(void) { return 13; }
''')
        output, manifest_path, _ = self.generate([
            ('java.base', alpha), ('net', beta)])
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest['descriptor'], 'js_openjdk25_native_resolver')
        self.assertEqual(manifest['nm']['flags'],
                         ['-g', '--defined-only', '--format=posix'])
        self.assertEqual([item['sha256'] for item in manifest['archives']], [
            hashlib.sha256(alpha.read_bytes()).hexdigest(),
            hashlib.sha256(beta.read_bytes()).hexdigest(),
        ])
        self.assertEqual(manifest['global_duplicate_choices'], [{
            'symbol': 'shared_entry', 'selected_module': 'net',
            'reason': 'strong overrides weak', 'candidates': ['java.base', 'net'],
        }])

        harness = self.directory / 'resolver_host.c'
        harness.write_text(r'''
#include "javaswitch/native_resolver.h"

extern const JsNativeResolver js_openjdk25_native_resolver;

static int call0(uintptr_t address) {
    int (*function)(void) = (int (*)(void))address;
    return function();
}

int main(void) {
    uintptr_t address = 0;
    int available = 0;
    int (*on_load)(void *, void *);

    if (js_openjdk25_native_resolver.abi_version != JS_NATIVE_RESOLVER_ABI_VERSION) return 1;
    if (js_native_resolver_install(&js_openjdk25_native_resolver) != JS_OK) return 2;
    if (js_native_module_available("java.base", &available) != JS_OK || !available) return 3;
    if (js_native_module_available("missing", &available) != JS_UNAVAILABLE || available) return 4;
    if (js_native_symbol_lookup("java.base", "Java_demo_alpha_value", &address) != JS_OK ||
        call0(address) != 17) return 5;
    if (js_native_symbol_lookup("net", "Java_demo_beta_value", &address) != JS_OK ||
        call0(address) != 29) return 6;
    if (js_native_symbol_lookup("java.base", "JVM_probe_entry", &address) != JS_OK ||
        call0(address) != 41) return 7;
    if (js_native_symbol_lookup("java.base", "JIMAGE_probe_entry", &address) != JS_OK ||
        call0(address) != 43) return 8;
    if (js_native_symbol_lookup("java.base", "native_data", &address) != JS_OK ||
        *(int *)(uintptr_t)address != 31) return 9;
    if (js_native_symbol_lookup("java.base", "shared_entry", &address) != JS_OK ||
        call0(address) != 13) return 10;
    if (js_native_symbol_lookup(NULL, "shared_entry", &address) != JS_OK ||
        call0(address) != 13) return 11;
    if (js_native_symbol_lookup(NULL, "JVM_probe_entry", &address) != JS_OK ||
        call0(address) != 41) return 12;
    if (js_native_symbol_lookup("java.base", "JNI_OnLoad_jimage", &address) != JS_OK) return 13;
    on_load = (int (*)(void *, void *))address;
    if (on_load(NULL, NULL) != 0x00010008) return 14;
    address = 1;
    if (js_native_symbol_lookup("missing", "shared_entry", &address) != JS_UNAVAILABLE ||
        address != 0) return 15;
    address = 1;
    if (js_native_symbol_lookup("java.base", "unknown_entry", &address) != JS_UNAVAILABLE ||
        address != 0) return 16;
    return 0;
}
''')
        binary = self.directory / 'resolver-host'
        command('cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wl,--gc-sections',
                '-I', ROOT / 'include', ROOT / 'src/native_resolver.c', output,
                harness, alpha, beta, '-o', binary)
        command(binary)

    def test_ambiguous_or_conflicting_archive_definitions_are_rejected(self):
        strong_a = make_archive(self.directory, 'strong_a',
                                'int duplicate_strong(void) { return 1; }\n')
        strong_b = make_archive(self.directory, 'strong_b',
                                'int duplicate_strong(void) { return 2; }\n')
        result = self.generate([('one', strong_a), ('two', strong_b)],
                               name='strong-conflict', check=False)
        self.assertNotEqual(result[2].returncode, 0)
        self.assertIn('conflicting strong symbol', result[2].stderr)
        self.assertFalse(result[0].exists())
        self.assertFalse(result[1].exists())

        weak_a = make_archive(self.directory, 'weak_a',
                              '__attribute__((weak)) int duplicate_weak(void) { return 1; }\n')
        weak_b = make_archive(self.directory, 'weak_b',
                              '__attribute__((weak)) int duplicate_weak(void) { return 2; }\n')
        result = self.generate([('one', weak_a), ('two', weak_b)],
                               name='weak-conflict', check=False)
        self.assertNotEqual(result[2].returncode, 0)
        self.assertIn('ambiguous weak symbol', result[2].stderr)

    def test_per_module_symbol_filters_keep_only_requested_real_exports(self):
        libc = make_archive(self.directory, 'filtered_libc', r'''
extern int unsupported_newlib_syscall(void);
int newlib_strlen(void) { return 7; }
int newlib_memcpy(void) { return 9; }
int newlib_unavailable_syscall(void) { return unsupported_newlib_syscall(); }
''')
        libm = make_archive(self.directory, 'filtered_libm', r'''
int newlib_sin(void) { return 11; }
int newlib_cos(void) { return 13; }
''')
        output, manifest_path, _ = self.generate(
            [('libc', libc), ('libm', libm)], name='filtered',
            symbols=[('libc', ['newlib_strlen']), ('libc', ['newlib_memcpy']),
                     ('libm', ['newlib_sin'])])
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest['symbol_filters'], {
            'libc': ['newlib_memcpy', 'newlib_strlen'], 'libm': ['newlib_sin'],
        })
        self.assertEqual(manifest['resolver_symbol_count'], 3)
        self.assertIn('newlib_strlen', output.read_text())
        self.assertIn('newlib_memcpy', output.read_text())
        self.assertIn('newlib_sin', output.read_text())
        self.assertNotIn('newlib_cos', output.read_text())
        self.assertNotIn('newlib_unavailable_syscall', output.read_text())

        harness = self.directory / 'filtered_host.c'
        harness.write_text(r'''
#include "javaswitch/native_resolver.h"
extern const JsNativeResolver js_openjdk25_native_resolver;
int main(void) {
    uintptr_t address = 0;
    if (js_native_resolver_install(&js_openjdk25_native_resolver) != JS_OK) return 1;
    if (js_native_symbol_lookup("libc", "newlib_strlen", &address) != JS_OK ||
        ((int (*)(void))address)() != 7) return 2;
    if (js_native_symbol_lookup("libc", "newlib_memcpy", &address) != JS_OK ||
        ((int (*)(void))address)() != 9) return 3;
    if (js_native_symbol_lookup("libm", "newlib_sin", &address) != JS_OK ||
        ((int (*)(void))address)() != 11) return 4;
    address = 1;
    if (js_native_symbol_lookup("libm", "newlib_cos", &address) != JS_UNAVAILABLE || address != 0)
        return 5;
    address = 1;
    if (js_native_symbol_lookup("libc", "newlib_unavailable_syscall", &address) != JS_UNAVAILABLE ||
        address != 0) return 6;
    return 0;
}
''')
        binary = self.directory / 'filtered-host'
        linked = command('cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
                         '-Wl,--gc-sections',
                         '-I', ROOT / 'include', ROOT / 'src/native_resolver.c', output,
                         harness, libc, libm, '-o', binary, check=False)
        self.assertEqual(linked.returncode, 0, linked.stderr)
        command(binary)

    def test_symbol_filter_requires_each_safe_export_and_known_module(self):
        library = make_archive(self.directory, 'filter_validation',
                               'int present_export(void) { return 1; }\n')
        for symbols, expected in [
                ([('unknown', ['present_export'])], 'module(s) absent from --library'),
                ([('libc', ['missing_export'])], 'not found as safe, defined exports'),
                ([('libc', ['present_export', 'missing_export'])],
                 'not found as safe, defined exports')]:
            output, manifest, result = self.generate([('libc', library)],
                                                     name='invalid-' + str(len(symbols)) + expected[:3],
                                                     check=False, symbols=symbols)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(expected, result.stderr)
            self.assertFalse(output.exists())
            self.assertFalse(manifest.exists())

    def test_missing_archive_duplicate_module_and_missing_nm_fail_closed(self):
        library = make_archive(self.directory, 'simple',
                               'int Java_demo_value(void) { return 1; }\n')
        missing = self.directory / 'missing.a'
        result = self.generate([('gone', missing)], name='missing', check=False)
        self.assertNotEqual(result[2].returncode, 0)
        self.assertFalse(result[0].exists())
        self.assertFalse(result[1].exists())

        result = self.generate([('same', library), ('same', library)],
                               name='duplicate-module-input', check=False)
        # Exact repeated archive input is rejected as an ambiguous duplicate definition.
        self.assertNotEqual(result[2].returncode, 0)

        output = self.directory / 'bad-nm.c'
        manifest = self.directory / 'bad-nm.json'
        result = command('python3', GENERATOR, '--nm', self.directory / 'absent-nm',
                         '--output', output, '--manifest', manifest,
                         '--library', 'test=' + str(library), check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertFalse(manifest.exists())


if __name__ == '__main__':
    unittest.main()
