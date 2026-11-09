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

    def test_hotspot_os_protect_none_uncommitted_reaches_bridge(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = pathlib.Path(directory)
            stubs = {
                'runtime/os.hpp': r'''#pragma once
#include <cstddef>
#include <cstdint>
using address = unsigned char*;
using MemTag = int;
using size_change_t = int;
enum ProtType { MEM_PROT_NONE, MEM_PROT_READ, MEM_PROT_RW };
enum { OOM_MMAP_ERROR = 0 };
#define CALLER_PC 0
namespace os {
char* pd_reserve_memory(size_t, bool);
char* reserve_memory_aligned(size_t, size_t, MemTag, bool);
char* pd_attempt_reserve_memory_at(char*, size_t, bool);
bool pd_commit_memory(char*, size_t, bool);
bool pd_commit_memory(char*, size_t, size_t, bool);
void pd_commit_memory_or_exit(char*, size_t, bool, const char*);
void pd_commit_memory_or_exit(char*, size_t, size_t, bool, const char*);
bool pd_uncommit_memory(char*, size_t, bool);
bool pd_release_memory(char*, size_t);
bool protect_memory(char*, size_t, ProtType, bool = true);
bool guard_memory(char*, size_t);
bool unguard_memory(char*, size_t);
bool pd_create_stack_guard_pages(char*, size_t);
bool committed_in_range(address, size_t, address&, size_t&);
size_t pd_pretouch_memory(void*, void*, size_t);
void pd_realign_memory(char*, size_t, size_t);
void pd_disclaim_memory(char*, size_t);
void large_page_init();
size_t large_page_size();
bool can_commit_large_page_memory();
char* pd_reserve_memory_special(size_t, size_t, size_t, char*, bool);
bool pd_release_memory_special(char*, size_t);
bool can_trim_native_heap();
bool trim_native_heap(size_change_t*);
char* pd_map_memory(int, const char*, size_t, char*, size_t, bool, bool);
bool pd_unmap_memory(char*, size_t);
char* map_memory_to_file(char*, size_t, int);
char* map_memory_to_file_aligned(size_t, size_t, int, MemTag);
char* pd_attempt_map_memory_to_file_at(char*, size_t, int);
int create_file_for_heap(const char*);
}
void vm_exit_out_of_memory(size_t, int, const char*, ...);
void warning(const char*, ...);
''',
                'runtime/globals.hpp': r'''#pragma once
extern bool UseLargePages;
#define FLAG_SET_DEFAULT(flag, value) ((flag) = (value))
''',
                'runtime/java.hpp': '#pragma once\n',
                'nmt/memTracker.hpp': r'''#pragma once
#include "runtime/os.hpp"
class MemTracker {
 public:
  static void record_virtual_memory_reserve(address, size_t, int, MemTag) {}
};
''',
                'logging/log.hpp': r'''#pragma once
struct FakeWarningLog { void operator()(const char*, ...) const {} };
static FakeWarningLog fake_warning_log;
#define log_warning(...) fake_warning_log
''',
                'utilities/align.hpp': '#pragma once\n',
            }
            for relative, contents in stubs.items():
                path = temp / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            object_paths = []
            c_sources = [
                'src/horizon_alias.c',
                'src/vmem_horizon.c',
                'ports/openjdk25/src/hotspot/os/horizon/horizon_memory_bridge.c',
            ]
            for index, source in enumerate(c_sources):
                obj = temp / f'bridge-{index}.o'
                subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Itests/ffi_stubs', '-Iinclude',
                    '-Iports/openjdk25/src/hotspot/os/horizon', '-c', source,
                    '-o', str(obj)], cwd=ROOT, check=True)
                object_paths.append(str(obj))
            binary = temp / 'hotspot-os-protect-test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter',
                f'-I{temp}', '-Itests/ffi_stubs', '-Iinclude',
                '-Iports/openjdk25/src/hotspot/os/horizon', '-Itests',
                'tests/horizon_os_protect_host.cpp',
                'ports/openjdk25/src/hotspot/os/horizon/os_horizon_memory.cpp',
                *object_paths, '-pthread', '-o', str(binary)], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)
