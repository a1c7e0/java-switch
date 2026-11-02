/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/threadLocalStorage.hpp"
#include "utilities/debug.hpp"
#include "horizon_bridge.h"

// Initialized once by Threads::create_vm before any VM worker is created.
// The slot is process-lifetime, like the VM; no concurrent init/free contract.
static int horizon_thread_slot = -1;

static void restore_thread_pointer(void* pointer) {
  // libnx clears a slot before its destructor. Restore the value so a later
  // JNI/native destructor can detach using the still-associated Thread*.
  // Real detach must clear the association; this is not an automatic detach.
  ThreadLocalStorage::set_thread(static_cast<Thread*>(pointer));
}

void ThreadLocalStorage::init() {
  guarantee(!is_initialized(), "Horizon TLS initialized more than once");
  int slot = js_horizon_tls_create(restore_thread_pointer);
  guarantee(slot >= 0, "libnx threadTlsAlloc failed: no available TLS slot");
  horizon_thread_slot = slot;
}

bool ThreadLocalStorage::is_initialized() { return horizon_thread_slot >= 0; }

Thread* ThreadLocalStorage::thread() {
  guarantee(is_initialized(), "Horizon TLS accessed before initialization");
  return static_cast<Thread*>(js_horizon_tls_get(horizon_thread_slot));
}

void ThreadLocalStorage::set_thread(Thread* pointer) {
  guarantee(is_initialized(), "Horizon TLS accessed before initialization");
  js_horizon_tls_set(horizon_thread_slot, pointer);
}
