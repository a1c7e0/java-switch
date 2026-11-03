#!/usr/bin/env python3
"""Generate a static Java 25 native resolver from real archive definitions.

The output contains references only to symbols reported as globally defined
by the supplied nm tool. It never declares or implements JNI entry points.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

GENERATOR_VERSION = 2
NM_FLAGS = ('-g', '--defined-only', '--format=posix')
SAFE_IDENTIFIER = re.compile(r'^[A-Za-z][A-Za-z0-9_]*$')
SAFE_MODULE = re.compile(r'^[A-Za-z][A-Za-z0-9_.-]*$')
C_KEYWORDS = frozenset((
    'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do',
    'double', 'else', 'enum', 'extern', 'float', 'for', 'goto', 'if', 'inline',
    'int', 'long', 'register', 'restrict', 'return', 'short', 'signed', 'sizeof',
    'static', 'struct', 'switch', 'typedef', 'union', 'unsigned', 'void',
    'volatile', 'while', '_Alignas', '_Alignof', '_Atomic', '_Bool', '_Complex',
    '_Generic', '_Imaginary', '_Noreturn', '_Static_assert', '_Thread_local',
    'NULL', 'bool', 'ptrdiff_t', 'size_t', 'wchar_t', 'intmax_t', 'uintmax_t',
    'intptr_t', 'uintptr_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t',
    'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
))
STRONG_TYPES = frozenset('TDBRSCGu')
WEAK_TYPES = frozenset('WV')
INTERNAL_IDENTIFIERS = frozenset((
    'js_openjdk25_native_resolver',
    'js_o25_name_compare',
    'js_o25_lookup',
    'js_o25_module_available',
    'js_o25_resolve',
    'js_o25_symbol',
    'js_o25_module',
))


class GeneratorError(Exception):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def parse_library_spec(spec):
    if '=' not in spec:
        raise GeneratorError(f'--library must be module=/path/to/archive.a: {spec!r}')
    module, archive_text = spec.split('=', 1)
    if not SAFE_MODULE.fullmatch(module):
        raise GeneratorError(f'unsafe or empty module name in --library: {module!r}')
    if not archive_text:
        raise GeneratorError(f'archive path is empty for module {module!r}')
    archive = pathlib.Path(archive_text).expanduser().resolve()
    if not archive.is_file():
        raise GeneratorError(f'archive does not exist or is not a regular file: {archive}')
    return module, archive


def parse_symbol_spec(spec):
    if '=' not in spec:
        raise GeneratorError(f'--symbols must be module=name1,name2: {spec!r}')
    module, names_text = spec.split('=', 1)
    if not SAFE_MODULE.fullmatch(module):
        raise GeneratorError(f'unsafe or empty module name in --symbols: {module!r}')
    names = [name.strip() for name in names_text.split(',')]
    if not names or any(not name for name in names):
        raise GeneratorError(f'empty symbol name in --symbols for module {module!r}')
    for name in names:
        if (not SAFE_IDENTIFIER.fullmatch(name) or name in C_KEYWORDS or
                name in INTERNAL_IDENTIFIERS):
            raise GeneratorError(f'unsafe or unsupported symbol name in --symbols: {name!r}')
    return module, names


def nm_version(nm):
    result = subprocess.run([nm, '--version'], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode != 0:
        raise GeneratorError(f'nm --version failed for {nm!r}: {result.stdout.strip()}')
    return result.stdout.splitlines()[0] if result.stdout else 'unknown'


def parse_nm_output(output, module, archive):
    member = ''
    symbols = []
    unsafe = 0
    ignored_types = {}
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(':') and ('[' in stripped or stripped.startswith(str(archive))):
            member = stripped[:-1]
            continue
        fields = stripped.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            continue
        name, symbol_type = fields[0], fields[1]
        if symbol_type in STRONG_TYPES:
            strength = 'strong'
        elif symbol_type in WEAK_TYPES:
            strength = 'weak'
        else:
            ignored_types[symbol_type] = ignored_types.get(symbol_type, 0) + 1
            continue
        if (not SAFE_IDENTIFIER.fullmatch(name) or name in C_KEYWORDS or
                name in INTERNAL_IDENTIFIERS):
            unsafe += 1
            continue
        symbols.append({
            'module': module,
            'archive': str(archive),
            'member': member,
            'name': name,
            'type': symbol_type,
            'strength': strength,
        })
    return symbols, unsafe, ignored_types


def collect_archives(libraries, nm):
    version = nm_version(nm)
    records = []
    invocations = []
    all_symbols = []
    for module, archive in libraries:
        command = [nm, *NM_FLAGS, str(archive)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        if result.returncode != 0:
            raise GeneratorError(
                f'nm failed for {archive} (exit {result.returncode}): {result.stderr.strip()}')
        symbols, unsafe, ignored_types = parse_nm_output(result.stdout, module, archive)
        record = {
            'module': module,
            'path': str(archive),
            'sha256': sha256_file(archive),
            'defined_symbol_count': len(symbols) + unsafe + sum(ignored_types.values()),
            'resolver_symbol_count': len(symbols),
            'unsafe_identifier_count': unsafe,
            'ignored_type_counts': dict(sorted(ignored_types.items())),
        }
        records.append(record)
        all_symbols.extend(symbols)
        invocations.append({'argv': command, 'exit_status': result.returncode})
    return version, records, invocations, all_symbols


def apply_symbol_filters(libraries, symbols, symbol_filters):
    """Limit selected modules to explicit exports and require each request to exist."""
    module_names = {module for module, _ in libraries}
    unknown_modules = sorted(set(symbol_filters) - module_names)
    if unknown_modules:
        raise GeneratorError('--symbols refers to module(s) absent from --library: ' +
                             ', '.join(repr(module) for module in unknown_modules))

    requested_by_module = {
        module: set(names) for module, names in symbol_filters.items()
    }
    found_by_module = {module: set() for module in requested_by_module}
    filtered = []
    for symbol in symbols:
        requested = requested_by_module.get(symbol['module'])
        if requested is None or symbol['name'] in requested:
            filtered.append(symbol)
            if requested is not None:
                found_by_module[symbol['module']].add(symbol['name'])

    for module, requested in requested_by_module.items():
        missing = sorted(requested - found_by_module[module])
        if missing:
            raise GeneratorError(
                f'--symbols requested symbol(s) not found as safe, defined exports '
                f'in module {module!r}: {", ".join(missing)}')
    return filtered


def choose_definition(name, definitions, scope):
    strong = [item for item in definitions if item['strength'] == 'strong']
    weak = [item for item in definitions if item['strength'] == 'weak']
    if len(strong) > 1:
        origins = ', '.join(f"{item['module']}:{item['member']} ({item['type']})"
                            for item in strong)
        raise GeneratorError(f'conflicting strong symbol {name!r} in {scope}: {origins}')
    if strong:
        return strong[0], 'strong overrides weak' if weak else 'unique strong'
    if len(weak) > 1:
        origins = ', '.join(f"{item['module']}:{item['member']} ({item['type']})"
                            for item in weak)
        raise GeneratorError(f'ambiguous weak symbol {name!r} in {scope}: {origins}')
    return weak[0], 'unique weak'


def build_tables(libraries, symbols):
    modules = sorted({module for module, _ in libraries})
    grouped = {module: {} for module in modules}
    for symbol in symbols:
        grouped[symbol['module']].setdefault(symbol['name'], []).append(symbol)

    module_tables = {}
    for module in modules:
        entries = {}
        for name, definitions in grouped[module].items():
            selected, _ = choose_definition(name, definitions, f'module {module!r}')
            entries[name] = selected
        module_tables[module] = entries

    global_definitions = {}
    for module, entries in module_tables.items():
        for name, symbol in entries.items():
            global_definitions.setdefault(name, []).append(symbol)
    global_table = {}
    global_choices = []
    for name, definitions in global_definitions.items():
        selected, reason = choose_definition(name, definitions, 'global scope')
        global_table[name] = selected
        if len(definitions) > 1:
            global_choices.append({
                'symbol': name,
                'selected_module': selected['module'],
                'reason': reason,
                'candidates': [item['module'] for item in definitions],
            })
    return modules, module_tables, global_table, global_choices


def c_string(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def emit_source(modules, module_tables, global_table):
    source = [
        '/* Generated by scripts/generate-openjdk25-native-resolver.py; do not edit. */',
        '/* The table names only symbols present in the recorded static archives. */',
        '/* Addresses follow final static-link symbol selection for duplicate weak names. */',
        '#include <stddef.h>',
        '#include <stdint.h>',
        '#include "javaswitch/native_resolver.h"',
        '',
        'typedef struct { const char *name; uintptr_t address; } js_o25_symbol;',
        'typedef struct { const char *name; const js_o25_symbol *symbols; size_t count; } js_o25_module;',
        '',
    ]
    all_names = sorted({
        item['name'] for table in module_tables.values() for item in table.values()
    })
    aliases = {name: f'js_o25_address_{index}' for index, name in enumerate(all_names)}
    for name in all_names:
        # Bind an address-only alias without redeclaring standard C functions
        # as objects under their builtin names or inventing callable prototypes.
        source.append(f'extern const unsigned char {aliases[name]}[] __asm__({c_string(name)});')
    if all_names:
        source.append('')

    if global_table:
        source.append('static const js_o25_symbol js_o25_global_symbols[] = {')
        for name, item in sorted(global_table.items()):
            source.append(f'    {{{c_string(name)}, (uintptr_t)(const void *){aliases[name]}}},')
        source.extend(['};', ''])
    for index, module in enumerate(modules):
        table = module_tables[module]
        if table:
            source.append(f'static const js_o25_symbol js_o25_module_symbols_{index}[] = {{')
            for name in sorted(table):
                source.append(f'    {{{c_string(name)}, (uintptr_t)(const void *){aliases[name]}}},')
            source.extend(['};', ''])

    source.extend([
        'static int js_o25_name_compare(const char *left, const char *right) {',
        '    while (*left != \'\\0\' && *left == *right) { left++; right++; }',
        '    return (int)(unsigned char)*left - (int)(unsigned char)*right;',
        '}',
        '',
    ])
    source.append('static const js_o25_module js_o25_modules[] = {')
    for index, module in enumerate(modules):
        table = module_tables[module]
        pointer = f'js_o25_module_symbols_{index}' if table else 'NULL'
        source.append(f'    {{{c_string(module)}, {pointer}, {len(table)}u}},')
    source.extend(['};', ''])
    source.extend([
        'static const js_o25_symbol *js_o25_lookup(const js_o25_symbol *symbols,',
        '                                                size_t count, const char *name) {',
        '    size_t low = 0, high = count;',
        '    while (low < high) {',
        '        size_t middle = low + (high - low) / 2;',
        '        int order = js_o25_name_compare(name, symbols[middle].name);',
        '        if (order == 0) return &symbols[middle];',
        '        if (order < 0) high = middle; else low = middle + 1;',
        '    }',
        '    return NULL;',
        '}',
        '',
        'static const js_o25_module *js_o25_find_module(const char *name) {',
        '    size_t low = 0, high = sizeof(js_o25_modules) / sizeof(js_o25_modules[0]);',
        '    while (low < high) {',
        '        size_t middle = low + (high - low) / 2;',
        '        int order = js_o25_name_compare(name, js_o25_modules[middle].name);',
        '        if (order == 0) return &js_o25_modules[middle];',
        '        if (order < 0) high = middle; else low = middle + 1;',
        '    }',
        '    return NULL;',
        '}',
        '',
        'static int js_o25_module_available(const char *module, void *context) {',
        '    (void)context;',
        '    return module == NULL || js_o25_find_module(module) != NULL;',
        '}',
        '',
        'static uintptr_t js_o25_resolve(const char *module, const char *name, void *context) {',
        '    const js_o25_symbol *symbol;',
        '    (void)context;',
        '    if (module == NULL) {',
    ])
    if global_table:
        source.append('        symbol = js_o25_lookup(js_o25_global_symbols,')
        source.append('                                sizeof(js_o25_global_symbols) / sizeof(js_o25_global_symbols[0]), name);')
        source.append('        return symbol != NULL ? symbol->address : (uintptr_t)0;')
    else:
        source.append('        return (uintptr_t)0;')
    source.extend([
        '    }',
        '    {',
        '        const js_o25_module *selected = js_o25_find_module(module);',
        '        if (selected == NULL) return (uintptr_t)0;',
        '        symbol = js_o25_lookup(selected->symbols, selected->count, name);',
        '        return symbol != NULL ? symbol->address : (uintptr_t)0;',
        '    }',
        '}',
        '',
        'const JsNativeResolver js_openjdk25_native_resolver = {',
        '    JS_NATIVE_RESOLVER_ABI_VERSION,',
        '    sizeof(JsNativeResolver),',
        '    js_o25_module_available,',
        '    js_o25_resolve,',
        '    NULL',
        '};',
        '',
    ])
    return '\n'.join(source)


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as output:
            output.write(content)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', action='append', required=True,
                        metavar='MODULE=ARCHIVE',
                        help='explicit canonical module name and real static archive; repeatable')
    parser.add_argument('--symbols', action='append', default=[],
                        metavar='MODULE=NAME1,NAME2',
                        help='restrict one module to exact defined exports; repeat to add names')
    parser.add_argument('--nm', required=True, help='path to the nm tool for these archives')
    parser.add_argument('--output', required=True, type=pathlib.Path,
                        help='generated C source path')
    parser.add_argument('--manifest', required=True, type=pathlib.Path,
                        help='JSON generation metadata path')
    args = parser.parse_args(argv)
    try:
        nm = str(pathlib.Path(args.nm).expanduser().resolve())
        if not pathlib.Path(nm).is_file() or not os.access(nm, os.X_OK):
            raise GeneratorError(f'nm is missing or not executable: {nm}')
        libraries = [parse_library_spec(spec) for spec in args.library]
        seen_inputs = set()
        for module, archive in libraries:
            identity = (module, str(archive))
            if identity in seen_inputs:
                raise GeneratorError(
                    f'duplicate archive input for module {module!r}: {archive}')
            seen_inputs.add(identity)
        symbol_filters = {}
        for spec in args.symbols:
            module, names = parse_symbol_spec(spec)
            symbol_filters.setdefault(module, set()).update(names)
        nm_ver, archives, invocations, symbols = collect_archives(libraries, nm)
        symbols = apply_symbol_filters(libraries, symbols, symbol_filters)
        modules, module_tables, global_table, global_choices = build_tables(libraries, symbols)
        source = emit_source(modules, module_tables, global_table)
        script_path = pathlib.Path(__file__).resolve()
        manifest = {
            'schema_version': GENERATOR_VERSION,
            'descriptor': 'js_openjdk25_native_resolver',
            'address_model': ('symbol addresses use final static-link selection; a strong definition '
                              'that overrides a weak same-name definition is visible in each module table'),
            'generator': str(script_path),
            'generator_sha256': sha256_file(script_path),
            'nm': {
                'path': nm,
                'version': nm_ver,
                'flags': list(NM_FLAGS),
                'invocations': invocations,
            },
            'archives': archives,
            'modules': modules,
            'symbol_filters': {module: sorted(names)
                               for module, names in sorted(symbol_filters.items())},
            'resolver_symbol_count': sum(len(table) for table in module_tables.values()),
            'global_symbol_count': len(global_table),
            'global_duplicate_choices': global_choices,
            'source_sha256': hashlib.sha256(source.encode('utf-8')).hexdigest(),
        }
        source_path = args.output.expanduser().resolve()
        manifest_path = args.manifest.expanduser().resolve()
        if source_path == manifest_path:
            raise GeneratorError('--output and --manifest must be different paths')
        input_paths = {archive for _, archive in libraries} | {pathlib.Path(nm)}
        if source_path in input_paths or manifest_path in input_paths:
            raise GeneratorError('generated outputs must not overwrite nm or an input archive')
        atomic_write(source_path, source)
        atomic_write(manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    except (GeneratorError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
