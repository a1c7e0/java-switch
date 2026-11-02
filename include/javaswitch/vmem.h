#ifndef JAVASWITCH_VMEM_H
#define JAVASWITCH_VMEM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental page-range mappings. It is not yet a complete HotSpot OS port.
 * Callers must serialize release against any other access to the same region. */
typedef struct js_vmem js_vmem;
typedef enum { JS_VMEM_OK = 0, JS_VMEM_INVALID, JS_VMEM_STATE,
               JS_VMEM_NO_MEMORY, JS_VMEM_SYSTEM, JS_VMEM_UNSUPPORTED } js_vmem_status;
typedef enum { JS_VMEM_NONE = 1, JS_VMEM_READ = 2,
               JS_VMEM_READ_WRITE = 3, JS_VMEM_READ_EXECUTE = 4 } js_vmem_access;
/* Size must be a nonzero multiple of 4096. Returns an unmapped reservation. */
js_vmem_status js_vmem_reserve(size_t size, js_vmem **out);
/* Alignment must be a power of two >= 4096. No physical memory is allocated. */
js_vmem_status js_vmem_reserve_aligned(size_t size, size_t alignment, js_vmem **out);
void *js_vmem_address(const js_vmem *region);
size_t js_vmem_size(const js_vmem *region);
/* Offset/length must be page-aligned, nonempty and inside the reservation.
 * Commit requires all pages unmapped; uncommit/protect require all mapped.
 * Each range operation makes one syscall and changes state only on success.
 * Commit maps RW pages. Protect supports NONE/R/RW; executable access returns
 * UNSUPPORTED without a syscall. Use the separate libnx JIT closure allocator.
 * These syscalls may be denied by the Horizon process; preserve raw Result. */
js_vmem_status js_vmem_commit_range(js_vmem *, size_t offset, size_t length, uint32_t *result);
js_vmem_status js_vmem_uncommit_range(js_vmem *, size_t offset, size_t length, uint32_t *result);
js_vmem_status js_vmem_protect_range(js_vmem *, size_t offset, size_t length,
                                    js_vmem_access, uint32_t *result);
/* Result receives the Horizon Result code, or zero for local validation errors. */
js_vmem_status js_vmem_commit(js_vmem *region, uint32_t *result);
js_vmem_status js_vmem_uncommit(js_vmem *region, uint32_t *result);
/* Unmaps contiguous mapped runs. On failure, retains the region and remaining
 * mappings for retry; earlier successful unmaps stay unmapped. No rollback.
 * Releases the complete reservation only; partial release is not supported. */
js_vmem_status js_vmem_release(js_vmem *region, uint32_t *result);
#ifdef __cplusplus
}
#endif
#endif
