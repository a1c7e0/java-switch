/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2007, 2008, 2009, 2010 Red Hat, Inc.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 * Modified 2026-11-02 for Horizon Zero: CPU-only Zero frame and copy helpers.
 */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "runtime/frame.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"

#include <string.h>

frame JavaThread::pd_last_frame() {
  assert(has_last_Java_frame(), "must have last_Java_sp() when suspended");
  return frame(last_Java_fp(), last_Java_sp());
}

void JavaThread::cache_global_variables() {
  // Zero does not cache CPU-specific global variables.
}

namespace {

template <typename T>
void conjoint_atomic_copy(const T* from, T* to, size_t count) {
  if (count == 0 || from == to) return;

  const uintptr_t src = reinterpret_cast<uintptr_t>(from);
  const uintptr_t dst = reinterpret_cast<uintptr_t>(to);
  const bool backwards = dst > src && ((dst - src) / sizeof(T)) < count;
  if (backwards) {
    for (size_t i = count; i != 0; ) {
      --i;
      T value;
      __atomic_load(from + i, &value, __ATOMIC_RELAXED);
      __atomic_store(to + i, &value, __ATOMIC_RELAXED);
    }
  } else {
    for (size_t i = 0; i < count; ++i) {
      T value;
      __atomic_load(from + i, &value, __ATOMIC_RELAXED);
      __atomic_store(to + i, &value, __ATOMIC_RELAXED);
    }
  }
}

} // namespace

extern "C" {

int SpinPause() {
  // AArch64's YIELD is a scheduler hint; it need not change control flow.
  __asm__ volatile("yield" ::: "memory");
  return 1;
}

void _Copy_conjoint_jshorts_atomic(const jshort* from, jshort* to, size_t count) {
  conjoint_atomic_copy(from, to, count);
}

void _Copy_conjoint_jints_atomic(const jint* from, jint* to, size_t count) {
  conjoint_atomic_copy(from, to, count);
}

void _Copy_conjoint_jlongs_atomic(const jlong* from, jlong* to, size_t count) {
  conjoint_atomic_copy(from, to, count);
}

void _Copy_arrayof_conjoint_bytes(const HeapWord* from, HeapWord* to,
                                  size_t count) {
  memmove(to, from, count);
}

void _Copy_arrayof_conjoint_jshorts(const HeapWord* from, HeapWord* to,
                                    size_t count) {
  memmove(to, from, count * sizeof(jshort));
}

void _Copy_arrayof_conjoint_jints(const HeapWord* from, HeapWord* to,
                                  size_t count) {
  memmove(to, from, count * sizeof(jint));
}

void _Copy_arrayof_conjoint_jlongs(const HeapWord* from, HeapWord* to,
                                   size_t count) {
  memmove(to, from, count * sizeof(jlong));
}

} // extern "C"

frame os::current_frame() {
  // This is only a diagnostic stack anchor, not a walkable native frame.
  frame dummy;
  dummy.set_sp(reinterpret_cast<intptr_t*>(os::current_stack_pointer()));
  return dummy;
}

frame os::get_sender_for_C_frame(frame* fr) {
  // Zero has no native C-frame walker. Interpreter frames use frame::sender.
  ShouldNotCallThis();
  return frame();
}

char* os::non_memory_address_word() {
  // Keep the value outside any practical Horizon user address range.
  return reinterpret_cast<char*>(-1);
}

address os::fetch_frame_from_context(const void* context,
                                     intptr_t** sp, intptr_t** fp) {
  // Horizon has no POSIX ucontext ABI. Null outputs explicitly mark unavailable.
  (void)context;
  if (sp != nullptr) *sp = nullptr;
  if (fp != nullptr) *fp = nullptr;
  return nullptr;
}

frame os::fetch_frame_from_context(const void* context) {
  // Do not reinterpret HOS exception records as a POSIX or HotSpot frame.
  (void)context;
  return frame();
}
