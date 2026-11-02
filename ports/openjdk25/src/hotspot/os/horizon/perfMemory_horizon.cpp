/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
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
 * Modified 2026-11-02: implement private Horizon PerfMemory and self-attach.
 */

#include "classfile/vmSymbols.hpp"
#include "runtime/os.hpp"
#include "runtime/perfMemory.hpp"
#include "utilities/debug.hpp"

// Horizon PerfData is private to this process. The shared runtime disables
// shared PerfData for Horizon before this allocator is called.
void PerfMemory::create_memory_region(size_t size) {
  char* region = os::reserve_memory(size, mtInternal);
  if (region == nullptr) return;

  if (!os::commit_memory(region, size, !ExecMem)) {
    guarantee(os::release_memory(region, size),
              "Horizon could not release PerfData reservation after commit failure");
    return;
  }

  _start = region;
  _capacity = size;
}

void PerfMemory::delete_memory_region() {
  // PerfMemory::destroy may run while writer threads still hold counter
  // addresses. Private mode has no persistent name or file to remove, so keep
  // the active allocation valid until process exit.
}

void PerfMemory::attach(int vmid, char** addrp, size_t* sizep, TRAPS) {
  *addrp = nullptr;
  *sizep = 0;

  if (vmid == 0 || vmid == os::current_process_id()) {
    if (!is_usable() || start() == nullptr || capacity() == 0) {
      THROW_MSG(vmSymbols::java_lang_UnsupportedOperationException(),
                "Local PerfData is unavailable on Horizon");
    }
    *addrp = start();
    *sizep = capacity();
    return;
  }

  THROW_MSG(vmSymbols::java_lang_UnsupportedOperationException(),
            "Horizon does not support attaching to another process's PerfData");
}

void PerfMemory::detach(char* addr, size_t bytes) {
  // attach() only lends the current process's private buffer. The caller does
  // not own it, and VM teardown cannot invalidate addresses held by writers.
}
