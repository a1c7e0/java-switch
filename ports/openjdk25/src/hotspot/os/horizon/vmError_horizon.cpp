/* SPDX-License-Identifier: GPL-2.0-only */
/* Modified 2026-11-02: preserve libnx fatal handling without POSIX recovery. */

#include "jvm.h"
#include "runtime/os.hpp"
#include "utilities/ostream.hpp"
#include "utilities/vmError.hpp"

#include <atomic>
#include <stdint.h>

static std::atomic<uint64_t> horizon_reporter_thread_id{0};

void VMError::reporting_started() {
  horizon_reporter_thread_id.store(
      static_cast<uint64_t>(os::current_thread_id()), std::memory_order_relaxed);
}

void VMError::install_secondary_signal_handler() {
  jio_fprintf(stderr,
      "Horizon: POSIX secondary signal recovery is unavailable; "
      "the installed libnx exception entry remains responsible for native faults.\n");
}

void VMError::interrupt_reporting_thread() {
  const uint64_t reporter = horizon_reporter_thread_id.load(std::memory_order_relaxed);
  jio_fprintf(stderr,
      "Horizon: cannot interrupt error reporter thread 0x%llx without POSIX signals; terminating.\n",
      static_cast<unsigned long long>(reporter));
  os::die();
}

void VMError::check_failing_cds_access(outputStream* st, const void* context) {
#if INCLUDE_CDS
  if (st != nullptr && context != nullptr) {
    st->print_cr("Horizon CDS fault attribution is unavailable for libnx exception records.");
  }
#else
  (void)st;
  (void)context;
#endif
}
