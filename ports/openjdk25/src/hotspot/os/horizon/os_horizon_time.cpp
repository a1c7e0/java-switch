/* SPDX-License-Identifier: GPL-2.0-only */
// Horizon timing primitives for the OpenJDK 25 OS port.
// This translation unit does not implement VM initialization or fault handling.
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "jvmti.h"
#include "horizon_bridge.h"
#include <sys/time.h>

jlong os::javaTimeNanos() {
  const uint64_t frequency = js_horizon_clock_frequency();
  const uint64_t ticks = js_horizon_clock_ticks();
  guarantee(frequency != 0, "Horizon system counter frequency is zero");
  // Avoid libnx armTicksToNs(ticks)'s multiply-before-divide overflow after
  // about 15 years. Unsigned arithmetic keeps nanosecond wrapping defined.
  const uint64_t seconds = ticks / frequency;
  const uint64_t remainder = ticks % frequency;
  return static_cast<jlong>(seconds * UINT64_C(1000000000) +
      static_cast<uint64_t>((static_cast<unsigned __int128>(remainder) *
                             UINT64_C(1000000000)) / frequency));
}

void os::javaTimeNanos_info(jvmtiTimerInfo* info) {
  info->max_value = all_bits_jlong;
  info->may_skip_backward = false;
  info->may_skip_forward = false;
  info->kind = JVMTI_TIMER_ELAPSED;
}

void os::javaTimeSystemUTC(jlong& seconds, jlong& nanos) {
  timeval now;
  const int result = gettimeofday(&now, nullptr);
  guarantee(result == 0, "Horizon gettimeofday failed: %d", errno);
  seconds = now.tv_sec;
  nanos = static_cast<jlong>(now.tv_usec) * 1000;
}

jlong os::javaTimeMillis() {
  jlong seconds, nanos;
  javaTimeSystemUTC(seconds, nanos);
  return seconds * 1000 + nanos / 1000000;
}
