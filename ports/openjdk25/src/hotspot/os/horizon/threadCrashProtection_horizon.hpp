/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_THREADCRASHPROTECTION_HORIZON_HPP
#define OS_HORIZON_THREADCRASHPROTECTION_HORIZON_HPP

#include "utilities/macros.hpp"

#if INCLUDE_JFR
#error Horizon JFR sampler crash protection is not implemented
#endif

class Thread;

// The pinned JDK25 has no callback-protection caller in this JFR-disabled
// configuration. Shared mutex/allocation assertions ask only whether protection
// is active; it cannot be active here. No protected callback execution/recovery
// API is supplied. Enabling JFR requires a real implementation above.
class ThreadCrashProtection final {
  ThreadCrashProtection() = delete;
public:
  static bool is_crash_protected(Thread*) { return false; }
};

#endif // OS_HORIZON_THREADCRASHPROTECTION_HORIZON_HPP
