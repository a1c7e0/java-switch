/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_CPU_HORIZON_ZERO_PREFETCH_HORIZON_ZERO_INLINE_HPP
#define OS_CPU_HORIZON_ZERO_PREFETCH_HORIZON_ZERO_INLINE_HPP
#include "runtime/prefetch.hpp"
inline void Prefetch::read(const void* address, intx interval) {
  __builtin_prefetch(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(address) + interval), 0, 3);
}
inline void Prefetch::write(void* address, intx interval) {
  __builtin_prefetch(reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(address) + interval), 1, 3);
}
#endif
