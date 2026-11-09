# Horizon physical-memory backend

The public `javaswitch/vmem.h` ABI is unchanged. Its existing reserve/commit,
partial mapping and NONE/R/RW protection calls now use the actual Alias region.
The private HotSpot bridge additionally implements partial reservation release
and a committed-range query over actual kernel memory information. Executable
requests remain unsupported; Zero's data-only code cache is handled separately.

## Address eligibility and shared ownership

libnx `virtmemFindAslr` explicitly excludes Alias memory. It therefore cannot
choose addresses accepted by `svcMapPhysicalMemory`. The shared implementation
in `src/horizon_alias.c` queries AliasRegionAddress/Size, applies the optional
AliasRegionExtraSize adjustment used by libnx, walks `svcQueryMemory` spans,
and records real `virtmemAddReservation` tokens. Alignment, overflow, actual
mapped spans, exhaustion and raw syscall errors are checked.

The SDK allocator and HotSpot use the same process-wide coordinator so their
unmapped software reservations cannot overlap. Caller-local locks precede the
coordinator lock, which precedes libnx's virtmem lock. A partial release prepares
left/right replacements while retaining the parent, unmaps the requested pages,
then publishes the replacements. Allocation or unmap failures retain the owner;
rollback does not expose an uncommitted ownership hole. The coordinator cannot
see third-party software reservations created directly in Alias without using
it. Ordinary libnx ASLR/stack/code finders exclude this region.

The public kernel-reference audit used Atmosphere commit
`6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`:

* `libraries/libmesosphere/source/svc/kern_svc_physical_memory.cpp` requires
  Alias placement and nonzero process system resources for physical mapping.
* `libraries/libmesosphere/include/mesosphere/kern_k_memory_manager.hpp` and
  `board/nintendo/nx/kern_k_memory_layout.hpp` describe the pool's 4 KiB minimum.
* `libraries/libmesosphere/source/kern_k_page_table_base.cpp` maps these pages
  as Normal memory with the reprotection capability used by NONE/R/RW changes.

This source audit does not establish the actual homebrew process permissions.
No kernel changes or capability bypasses are included. Mapping failures preserve
the real Horizon Result.

The ordinary `svcMapPhysicalMemory` and `svcUnmapPhysicalMemory` also require
the running process to have nonzero `SystemResourceSize` in its process metadata.
The kernel returns `0xfa01` when that resource is zero; lowering the newlib heap
or finding a different Alias address cannot supply it. The public allocator maps
that specific result to `JS_VMEM_UNSUPPORTED` while retaining `0xfa01` for
diagnostics. A homebrew loader or title must create the process with a nonzero
system-resource budget for these syscalls to work. Privileged unsafe mapping is
not a supported substitute. Hardware behavior remains to be verified on the
target process configuration.

The private HotSpot bridge has a separate fallback for processes whose checked
`SystemResourceSizeTotal` is zero. It eagerly obtains an aligned heap allocation
for the entire reserved range, initially protects it as NONE, and uses only
`svcSetMemoryPermission` for logical NONE/R/RW transitions. Commit makes a range
RW and zeros it; uncommit makes it NONE while keeping the backing allocated and
charged to the C heap. A partial release retains the complete original backing
until its last live split is released. Final release restores RW before freeing;
on a permission error it keeps ownership so cleanup can be retried. This mode
does not provide sparse physical commitment: the complete request must fit in
the process heap at reserve time. The public SDK reservation API remains
unmapped and does not use this fallback.

An uncommitted `MEM_PROT_NONE` request is also supported for this eager backing.
It performs the real kernel permission call while leaving the bridge's logical
committed-page map unchanged. This is needed by HotSpot's compressed-oop guard
setup. Read or read/write protection still requires committed pages. For an
Alias reservation, the bridge verifies uncommitted runs are already
`MemType_Unmapped` with `Perm_None` and protects committed runs with the actual
permission syscall. It never sets permission on an unmapped address. Query
errors and foreign mappings fail. A failed syscall preserves that run's tracked
permissions; earlier completed runs retain their accurate updates. Protection
never changes the committed-page map. The host regression also compiles the
maintained HotSpot `os::protect_memory` wrapper and exercises this exact
`committed=false` call through the real bridge; host kernel protection is
observed with a faulting child process. Horizon syscall behavior remains a
separate physical test.

## Memory budgets and verification

libnx's default zero `__nx_heap_size` commits almost the entire process budget
to newlib with `svcSetHeapSize`. A VM launcher must leave budget for Alias-backed
Java heap, metadata and VM regions. The experimental VM launcher sets a 256 MiB
newlib heap and logs any loader heap override, which takes precedence. This is
an initial application-mode smoke policy, not a Minecraft memory estimate.

`make host-test` includes SDK/VM shared-allocation failure tests. The dedicated
HotSpot memory suite also checks partial release/rollback, retained ownership,
permission transitions and committed-range clipping with libnx mocks. Target
objects and the SDK cross-compile. `zero-services-test.nro` L is the standalone
private-memory probe, including SDK/VM ownership checks; ffi-test Y/B retain
their existing public SDK actions. All Switch hardware behavior is NOT TESTED.
