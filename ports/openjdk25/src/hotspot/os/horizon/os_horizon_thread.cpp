/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "runtime/javaThread.inline.hpp"
#include "runtime/osThread.hpp"
#include "runtime/threadLocalStorage.hpp"
#include "logging/log.hpp"
#include "utilities/align.hpp"
#include "horizon_thread_bridge.h"

// Initial Zero bounds match the CPU-only 64 KiB floor used by upstream Zero;
// shared set_minimum_stack_sizes adds the VM's shadow/guard budgets. libnx
// allocates TLS/reent backing separately from the requested native stack.
size_t os::_compiler_thread_min_stack_allowed = 64 * K;
size_t os::_java_thread_min_stack_allowed = 64 * K;
size_t os::_vm_internal_thread_min_stack_allowed = 64 * K;
size_t os::_os_min_stack_allowed = 4096;

intx os::current_thread_id() {
  uint64_t id = 0; uint32_t raw = 0;
  auto status = js_horizon_thread_current_id(&id, &raw);
  guarantee(status == JS_HORIZON_THREAD_OK,
            "Horizon current thread ID failed: status=%d Result=0x%x", status, raw);
  return static_cast<intx>(id);
}

void os::current_stack_base_and_size(address* base, size_t* size) {
  uintptr_t low = 0, high = 0; uint32_t raw = 0;
  auto status = js_horizon_thread_current_stack_bounds(&low, &high, &raw);
  guarantee(status == JS_HORIZON_THREAD_OK,
            "Horizon current stack bounds failed: status=%d Result=0x%x", status, raw);
  *base = reinterpret_cast<address>(high);
  *size = high - low;
}

address os::current_stack_pointer() {
  uintptr_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return reinterpret_cast<address>(sp);
}

static void horizon_native_entry(void* context) {
  auto* thread = static_cast<Thread*>(context);
  OSThread* native = thread->osthread();
  uintptr_t low = 0, high = 0; uint32_t raw = 0;
  auto status = js_horizon_thread_current_stack_bounds(&low, &high, &raw);
  uint64_t id = 0;
  if (status == JS_HORIZON_THREAD_OK) status = js_horizon_thread_current_id(&id, &raw);
  if (status != JS_HORIZON_THREAD_OK) {
    native->set_state(ZOMBIE);
    native->startup()->report_and_wait(status, raw);
    return; // No Thread TLS was installed, and callback context is no longer used.
  }
  thread->record_stack_base_and_size();
  thread->initialize_thread_current();
  native->set_thread_id(id);
  native->set_state(INITIALIZED);
  native->startup()->report_and_wait(JS_HORIZON_THREAD_OK, 0);
  // Thread construction/publication has completed before parent releases us.
  thread->call_run();
  // call_run may destroy both Thread and OSThread. Do not dereference either.
  // The bridge callback returns into libnx threadExit before the external
  // native reaper is permitted to unmap this stack.
}

static void transfer_native(OSThread* thread) {
  auto* handle = thread->native_thread();
  if (handle == nullptr) return; // Main/JNI-attached native threads are borrowed.
  uint32_t raw = 0;
  auto status = js_horizon_thread_retire(handle, &raw);
  guarantee(status == JS_HORIZON_THREAD_OK,
            "Horizon native ownership transfer failed: status=%d Result=0x%x", status, raw);
  thread->set_native_thread(nullptr);
}

bool os::create_thread(Thread* thread, ThreadType kind, size_t requested) {
  assert(thread->osthread() == nullptr, "caller responsible");
  guarantee(ThreadLocalStorage::is_initialized(), "Horizon worker before TLS initialization");
  size_t stack = requested;
  size_t minimum = _vm_internal_thread_min_stack_allowed;
  if (kind == java_thread) {
    minimum = _java_thread_min_stack_allowed;
    if (stack == 0) stack = JavaThread::stack_size_at_create();
  } else if (kind == compiler_thread) {
    minimum = _compiler_thread_min_stack_allowed;
    if (stack == 0 && CompilerThreadStackSize > 0) {
      if (size_t(CompilerThreadStackSize) > SIZE_MAX / K) return false;
      stack = size_t(CompilerThreadStackSize) * K;
    }
    if (stack == 0) stack = 4 * M;
  } else if (stack == 0 && VMThreadStackSize > 0) {
    if (size_t(VMThreadStackSize) > SIZE_MAX / K) return false;
    stack = size_t(VMThreadStackSize) * K;
  }
  if (stack == 0) stack = M;
  stack = MAX2(stack, minimum);
  if (stack > SIZE_MAX - 4095) return false;
  stack = align_up(stack, size_t(4096));
  if (stack > 256 * M) return false; // Same explicit request cap as the native bridge.

  uint32_t raw = 0;
  auto status = js_horizon_thread_ensure_reaper_started(&raw);
  if (status != JS_HORIZON_THREAD_OK) {
    log_warning(os, thread)("Horizon reaper startup failed: status=%d Result=0x%x", status, raw);
    return false;
  }
  auto* native = new (std::nothrow) OSThread();
  if (native == nullptr) return false;
  thread->set_osthread(native);
  js_horizon_thread* handle = nullptr;
  bool started = false;
  status = js_horizon_thread_create(horizon_native_entry, thread, stack, &handle, &raw);
  if (status == JS_HORIZON_THREAD_OK) {
    native->set_native_thread(handle);
    status = js_horizon_thread_start(handle, &raw);
    if (status == JS_HORIZON_THREAD_OK) {
      started = true;
      status = native->startup()->wait_until_ready(&raw);
    }
  }
  if (status != JS_HORIZON_THREAD_OK) {
    log_warning(os, thread)("Horizon worker startup failed: status=%d Result=0x%x stack=%zu", status, raw, stack);
    if (started) {
      // A failed readiness report wakes the parent before the child has fully
      // returned through libnx. Join before freeing its gate/callback context.
      auto joined = js_horizon_thread_join(handle, UINT64_MAX, &raw);
      guarantee(joined == JS_HORIZON_THREAD_OK,
                "Horizon failed-start worker join failed: status=%d Result=0x%x", joined, raw);
    }
    transfer_native(native);
    thread->set_osthread(nullptr);
    delete native;
    return false;
  }
  return true; // Worker is ready but cannot call_run until pd_start_thread.
}

void os::pd_start_thread(Thread* thread) {
  guarantee(thread->osthread() != nullptr && thread->osthread()->native_thread() != nullptr,
            "Horizon start requires an owned native worker");
  thread->osthread()->startup()->release();
}

bool os::create_attached_thread(JavaThread* thread) {
  assert(thread->osthread() == nullptr, "already attached");
  DEBUG_ONLY(thread->verify_not_published();)
  uint64_t id = 0; uintptr_t low = 0, high = 0; uint32_t raw = 0;
  auto status = js_horizon_thread_current_id(&id, &raw);
  if (status == JS_HORIZON_THREAD_OK) status = js_horizon_thread_current_stack_bounds(&low, &high, &raw);
  if (status != JS_HORIZON_THREAD_OK) {
    log_warning(os, thread)("Horizon attach query failed: status=%d Result=0x%x", status, raw);
    return false;
  }
  auto* native = new (std::nothrow) OSThread();
  if (native == nullptr) return false;
  native->set_thread_id(id);
  native->set_state(RUNNABLE);
  thread->set_osthread(native);
  return true; // Current stack/handle remain owned by the caller/libnx.
}

bool os::create_main_thread(JavaThread* thread) { return create_attached_thread(thread); }

void os::free_thread(OSThread* native) {
  guarantee(native != nullptr, "Horizon missing OSThread");
  transfer_native(native);
  delete native;
}
