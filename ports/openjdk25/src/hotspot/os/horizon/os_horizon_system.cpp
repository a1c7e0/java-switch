/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/globals.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "utilities/globalDefinitions.hpp"
#include "horizon_system_bridge.h"

#include <limits.h>

static jlong horizon_elapsed_origin = 0;
static bool horizon_elapsed_origin_set = false;

/* os::init calls this once after libnx startup and before VM services begin. */
extern "C" void js_horizon_os_elapsed_time_init() {
  horizon_elapsed_origin = os::javaTimeNanos();
  horizon_elapsed_origin_set = true;
}

int os::active_processor_count() {
  if (ActiveProcessorCount > 0) return ActiveProcessorCount;

  uint64_t mask = 0;
  uint32_t raw = 0;
  if (js_horizon_system_core_mask(&mask, &raw) != JS_HORIZON_SYSTEM_OK) {
    return 0;
  }
  int count = 0;
  while (mask != 0) {
    count += static_cast<int>(mask & 1u);
    mask >>= 1;
  }
  return count;
}

int os::current_process_id() {
  uint64_t process_id = 0;
  uint32_t raw = 0;
  if (js_horizon_system_process_id(&process_id, &raw) != JS_HORIZON_SYSTEM_OK ||
      process_id > static_cast<uint64_t>(INT_MAX)) {
    return -1;
  }
  return static_cast<int>(process_id);
}

bool os::is_primordial_thread() {
  uint32_t primordial = 0;
  uint32_t raw = 0;
  return js_horizon_system_is_primordial(&primordial, &raw) ==
             JS_HORIZON_SYSTEM_OK && primordial != 0;
}

static bool horizon_process_memory(uint64_t* total, uint64_t* used) {
  uint32_t raw = 0;
  return js_horizon_system_memory(total, used, &raw) == JS_HORIZON_SYSTEM_OK;
}

physical_memory_size_type os::physical_memory() {
  uint64_t total = 0, used = 0;
  return horizon_process_memory(&total, &used)
      ? static_cast<physical_memory_size_type>(total) : 0;
}

bool os::has_allocatable_memory_limit(size_t* limit) {
  if (limit == nullptr) return false;
  uint64_t total = 0, used = 0;
  if (!horizon_process_memory(&total, &used) || total > SIZE_MAX) return false;
  *limit = static_cast<size_t>(total);
  return true;
}

bool os::available_memory(physical_memory_size_type& value) {
  uint64_t total = 0, used = 0;
  if (!horizon_process_memory(&total, &used)) return false;
  value = static_cast<physical_memory_size_type>(used >= total ? 0 : total - used);
  return true;
}

bool os::free_memory(physical_memory_size_type& value) {
  return available_memory(value);
}

size_t os::rss() {
  uint64_t total = 0, used = 0;
  if (!horizon_process_memory(&total, &used) || used > SIZE_MAX) return 0;
  return static_cast<size_t>(used);
}

size_t os::vm_min_address() {
  uint64_t base = 0;
  uint32_t raw = 0;
  const js_horizon_system_status status =
      js_horizon_system_aslr_base(&base, &raw);
  guarantee(status == JS_HORIZON_SYSTEM_OK && base <= SIZE_MAX,
            "Horizon ASLR region query failed: status=%d Result=0x%x base=0x%llx",
            status, raw, static_cast<unsigned long long>(base));
  return static_cast<size_t>(base);
}

jlong os::elapsed_counter() {
  const jlong now = javaTimeNanos();
  const uint64_t now_ticks = static_cast<uint64_t>(now);
  const uint64_t origin_ticks = static_cast<uint64_t>(horizon_elapsed_origin);
  return static_cast<jlong>(horizon_elapsed_origin_set
      ? now_ticks - origin_ticks : now_ticks);
}

jlong os::elapsed_frequency() {
  return 1000000000;
}

double os::elapsedTime() {
  return static_cast<double>(elapsed_counter()) /
         static_cast<double>(elapsed_frequency());
}

jlong os::current_thread_cpu_time() {
  return -1;
}

bool os::is_thread_cpu_time_supported() {
  return false;
}

OSReturn os::set_native_priority(Thread*, int) {
  return OS_ERR;
}

OSReturn os::get_native_priority(const Thread* const, int*) {
  return OS_ERR;
}

void os::naked_short_nanosleep(jlong nanoseconds) {
  if (nanoseconds <= 0) {
    js_horizon_system_sleep(0);
    return;
  }
  js_horizon_system_sleep(static_cast<uint64_t>(nanoseconds));
}

void os::naked_short_sleep(jlong milliseconds) {
  if (milliseconds <= 0) {
    js_horizon_system_sleep(0);
    return;
  }
  const uint64_t ms = static_cast<uint64_t>(milliseconds);
  const uint64_t nanos = ms > UINT64_MAX / UINT64_C(1000000)
      ? UINT64_MAX : ms * UINT64_C(1000000);
  js_horizon_system_sleep(nanos);
}

void os::naked_yield() {
  js_horizon_system_sleep(0);
}

void os::infinite_sleep() {
  for (;;) js_horizon_system_sleep(INT64_MAX);
}
