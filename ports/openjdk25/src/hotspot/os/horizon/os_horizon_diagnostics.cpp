/* SPDX-License-Identifier: GPL-2.0-only */
/* Modified 2026-11-02: Horizon Zero diagnostic support. */

#include "jvm.h"
#include "runtime/os.hpp"
#include "utilities/decoder_elf.hpp"
#include "utilities/ostream.hpp"
#include "utilities/permitForbiddenFunctions.hpp"
#include "horizon_fault_context.h"

#include <cxxabi.h>
#include <atomic>
#include <stdio.h>

extern "C" char __code_start[];
extern "C" char __rodata_start[];

bool os::address_is_in_vm(address addr) {
  const uintptr_t low = reinterpret_cast<uintptr_t>(__code_start);
  const uintptr_t high = reinterpret_cast<uintptr_t>(__rodata_start);
  const uintptr_t value = reinterpret_cast<uintptr_t>(addr);
  return value >= low && value < high;
}

bool os::find(address addr, outputStream* st) {
  if (st == nullptr) return false;
  st->print(PTR_FORMAT " (Horizon address; symbol lookup unavailable)", p2i(addr));
  st->cr();
  return false; // A raw address was printed, but no symbol/location was found.
}

void os::get_summary_cpu_info(char* buf, size_t buflen) {
  if (buf == nullptr || buflen == 0) return;
  os::snprintf(buf, buflen, "AArch64 (Zero interpreter)");
  buf[buflen - 1] = '\0';
}

void os::get_summary_os_info(char* buf, size_t buflen) {
  if (buf == nullptr || buflen == 0) return;
  os::snprintf(buf, buflen, "Horizon");
  buf[buflen - 1] = '\0';
}

void os::pd_print_cpu_info(outputStream* st, char*, size_t) {
  st->print_cr("CPU architecture: AArch64; model and feature details unavailable");
}

void os::print_os_info(outputStream* st) {
  st->print_cr("OS:");
  st->print_cr("Horizon (Nintendo Switch runtime; POSIX kernel details unavailable)");
  const int processors = os::active_processor_count();
  if (processors > 0) st->print_cr("Active processors: %d", processors);
  else st->print_cr("Active processors: unavailable");
  st->print_cr("Page size: %zu bytes", os::vm_page_size());
}

void os::print_memory_info(outputStream* st) {
  const uint64_t total = static_cast<uint64_t>(os::physical_memory());
  physical_memory_size_type available = 0;
  const bool has_available = os::available_memory(available);
  st->print("Memory: %zuK page", os::vm_page_size() >> 10);
  if (total != 0) {
    st->print(", physical %lluK", static_cast<unsigned long long>(total >> 10));
    if (has_available) {
      st->print(" (%lluK available)",
                static_cast<unsigned long long>(available >> 10));
    }
  } else {
    st->print(", physical memory unavailable");
  }
  const size_t process_used = os::rss();
  if (process_used != 0) {
    st->print(", process memory %zuK", process_used >> 10);
  }
  st->cr();
}

void os::print_memory_mappings(char* addr, size_t bytes, outputStream* st) {
  if (addr == nullptr) {
    st->print_cr("Horizon memory mappings unavailable (svcQueryMemory bridge not provided)");
  } else {
    st->print_cr("Horizon mapping query for " PTR_FORMAT " (%zu bytes) unavailable "
                 "(svcQueryMemory bridge not provided)", p2i(addr), bytes);
  }
}

void os::print_user_info(outputStream* st) {
  st->print_cr("User identity: unavailable on Horizon");
}

void os::print_signal_handlers(outputStream* st, char*, size_t) {
  st->print_cr("Horizon uses the libnx thread-exception entry; POSIX signal handlers are unavailable");
}

void os::print_siginfo(outputStream* st, const void* siginfo) {
  st->print("Horizon fault information: ");
  if (siginfo == nullptr) {
    st->print_cr("no platform siginfo record");
  } else {
    st->print_cr("opaque libnx exception record at " PTR_FORMAT
                 " (POSIX siginfo_t interpretation is unavailable)", p2i(siginfo));
  }
}

static bool read_horizon_context(const void* context,
                                 js_horizon_fault_context* summary,
                                 outputStream* st) {
  if (js_horizon_fault_context_read(context, summary) != 0) return true;
  st->print_cr(context == nullptr
      ? "Horizon exception context: unavailable (null record)"
      : "Horizon exception context: unsupported record or non-AArch64 state");
  return false;
}

void os::print_context(outputStream* st, const void* context) {
  js_horizon_fault_context summary;
  if (!read_horizon_context(context, &summary, st)) return;
  st->print_cr("Horizon ThreadExceptionDump (AArch64):");
  for (int i = 0; i < 29; i += 4) {
    const int end = MIN2(i + 4, 29);
    for (int reg = i; reg < end; ++reg) {
      st->print("x%-2d=%016llx%s", reg,
                static_cast<unsigned long long>(summary.gpr[reg]),
                reg + 1 == end ? "" : "  ");
    }
    st->cr();
  }
  st->print_cr("fp=%016llx lr=%016llx sp=%016llx pc=%016llx",
      static_cast<unsigned long long>(summary.fp),
      static_cast<unsigned long long>(summary.lr),
      static_cast<unsigned long long>(summary.sp),
      static_cast<unsigned long long>(summary.pc));
  st->print_cr("pstate=%08x esr=%08x far=%016llx descriptor=0x%x",
      summary.pstate, summary.esr,
      static_cast<unsigned long long>(summary.far), summary.error_desc);
}

void os::print_register_info(outputStream* st, const void* context,
                             int& continuation) {
  js_horizon_fault_context summary;
  if (!read_horizon_context(context, &summary, st)) {
    continuation = 29;
    return;
  }
  if (continuation < 0 || continuation > 29) {
    st->print_cr("Horizon register mapping: invalid continuation index %d", continuation);
    continuation = 29;
    return;
  }
  const int end = MIN2(continuation + 8, 29);
  while (continuation < end) {
    const int reg = continuation++;
    st->print_cr("x%-2d=%016llx (register value; memory mapping unavailable)", reg,
                 static_cast<unsigned long long>(summary.gpr[reg]));
  }
  if (continuation == 29) {
    st->print_cr("x29/fp=%016llx x30/lr=%016llx sp=%016llx pc=%016llx",
        static_cast<unsigned long long>(summary.fp),
        static_cast<unsigned long long>(summary.lr),
        static_cast<unsigned long long>(summary.sp),
        static_cast<unsigned long long>(summary.pc));
  }
}

int os::get_native_stack(address* stack, int size, int) {
  if (stack != nullptr) {
    for (int i = 0; i < size; ++i) stack[i] = nullptr;
  }
  // Horizon has no safe public native unwinder for arbitrary fatal contexts.
  return 0;
}

const char* os::exception_name(int exception_code, char* buf, size_t buflen) {
  const char* name = nullptr;
  switch (exception_code) {
    case 0x100: name = "InstructionAbort"; break;
    case 0x101: name = "Other"; break;
    case 0x102: name = "MisalignedPC"; break;
    case 0x103: name = "MisalignedSP"; break;
    case 0x104: name = "Trap"; break;
    case 0x106: name = "SError"; break;
    case 0x301: name = "BadSVC"; break;
    default: return nullptr;
  }
  if (buf == nullptr || buflen == 0) return nullptr;
  os::snprintf(buf, buflen, "%s", name);
  buf[buflen - 1] = '\0';
  return buf;
}

bool ElfDecoder::demangle(const char* symbol, char* buf, int buflen) {
  if (symbol == nullptr || buf == nullptr || buflen <= 0) return false;
  int status = 0;
  char* result = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
  if (result == nullptr) return false;
  const int written = os::snprintf(buf, static_cast<size_t>(buflen), "%s", result);
  permit_forbidden_function::free(result);
  return status == 0 && written >= 0 && written < buflen;
}
