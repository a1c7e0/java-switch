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
