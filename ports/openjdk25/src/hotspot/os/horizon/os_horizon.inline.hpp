/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_OS_HORIZON_INLINE_HPP
#define OS_HORIZON_OS_HORIZON_INLINE_HPP
#include "runtime/os.hpp"
#include "horizon_bridge.h"

// Conservative capability declarations: no recoverable Horizon fault handler
// exists yet. Do not enable implicit null traps or advertise stack guards.
inline bool os::zero_page_read_protected() { return false; }
inline bool os::uses_stack_guard_pages() { return false; }
inline bool os::must_commit_stack_guard_pages() { return false; }
inline void os::map_stack_shadow_pages(address sp) {
  // libnx threadCreate backs the complete fixed native stack before start;
  // there is no demand-grown stack/shadow mapping to expand here.
}
inline bool os::numa_has_group_homing() { return false; }
inline jlong os::rdtsc() { return static_cast<jlong>(js_horizon_clock_ticks()); }
#endif
