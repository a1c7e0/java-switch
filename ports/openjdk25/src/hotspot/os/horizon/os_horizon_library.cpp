/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "utilities/ostream.hpp"
#include "javaswitch/native_resolver.h"
#include <errno.h>
#include <string.h>

namespace {
struct HorizonStaticLibrary { char name[1]; };
char process_scope;
const char* module_name(const char* filename, char* buffer, size_t length) {
  if (filename == nullptr || *filename == '\0') return nullptr;
  const char* name = strrchr(filename, '/');
  name = name != nullptr ? name + 1 : filename;
  if (strncmp(name, "lib", 3) == 0) name += 3;
  size_t count = strlen(name);
  if (count > 3 && strcmp(name + count - 3, ".so") == 0) count -= 3;
  else if (count > 2 && strcmp(name + count - 2, ".a") == 0) count -= 2;
  if (count == 0 || count >= length) return nullptr;
  memcpy(buffer, name, count); buffer[count] = '\0';
  return buffer;
}
}

void* os::get_default_process_handle() { return &process_scope; }

// java.base's C native-library glue needs this exact process lookup token.
extern "C" void* js_horizon_process_handle() { return os::get_default_process_handle(); }

void* os::dll_load(const char* filename, char* error, int error_length) {
  char name[256];
  const char* module = module_name(filename, name, sizeof(name));
  int available = 0;
  if (module == nullptr || js_native_module_available(module, &available) != JS_OK || !available) {
    if (error != nullptr && error_length > 0)
      os::snprintf(error, size_t(error_length), "Horizon static module is not registered: %s", filename != nullptr ? filename : "(null)");
    errno = ENOENT; return nullptr;
  }
  const size_t bytes = sizeof(HorizonStaticLibrary) + strlen(module);
  auto* handle = static_cast<HorizonStaticLibrary*>(os::malloc(bytes, mtInternal));
  if (handle == nullptr) { errno = ENOMEM; return nullptr; }
  memcpy(handle->name, module, strlen(module) + 1);
  return handle;
}

void* os::dll_lookup(void* handle, const char* symbol) {
  if (handle == nullptr || symbol == nullptr) { errno = EINVAL; return nullptr; }
  const char* module = handle == &process_scope ? nullptr : static_cast<HorizonStaticLibrary*>(handle)->name;
  uintptr_t address = 0;
  if (js_native_symbol_lookup(module, symbol, &address) != JS_OK) { errno = ENOENT; return nullptr; }
  return reinterpret_cast<void*>(address);
}

void os::dll_unload(void* handle) {
  // Only the lookup token is owned here. Static functions and resolver context
  // remain resident for the VM lifetime, including cached JNI/FFM addresses.
  if (handle != nullptr && handle != &process_scope) os::free(handle);
}

void* os::lookup_function(const char* symbol) { return dll_lookup(get_default_process_handle(), symbol); }
void os::prepare_native_symbols() {
  // Named static lookup is installed before VM creation. No dynamic ELF loader
  // or implicit process-wide dlsym catalogue is claimed by this target.
}

bool os::dll_address_to_function_name(address, char* buffer, int length, int* offset, bool) {
  if (buffer != nullptr && length > 0) buffer[0] = '\0';
  if (offset != nullptr) *offset = -1;
  return false; // Reverse address lookup is unavailable; keep raw PCs in logs.
}
bool os::dll_address_to_library_name(address, char* buffer, int length, int* offset) {
  if (buffer != nullptr && length > 0) buffer[0] = '\0';
  if (offset != nullptr) *offset = -1;
  return false;
}
void os::print_dll_info(outputStream* stream) {
  char path[4096]; jvm_path(path, sizeof(path));
  stream->print_cr("Horizon static native image: %s", *path ? path : "(argv path unavailable)");
  stream->print_cr("Native libraries resolve through the installed static resolver; dynamic loading unavailable");
}
