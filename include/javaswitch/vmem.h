#ifndef JAVASWITCH_VMEM_H
#define JAVASWITCH_VMEM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental whole-region reservation. It is not yet a HotSpot OS port.
 * Callers must serialize release against any other access to the same region. */
typedef struct js_vmem js_vmem;
typedef enum { JS_VMEM_OK = 0, JS_VMEM_INVALID, JS_VMEM_STATE,
               JS_VMEM_NO_MEMORY, JS_VMEM_SYSTEM } js_vmem_status;
/* Size must be a nonzero multiple of 4096. Returns an unmapped reservation. */
js_vmem_status js_vmem_reserve(size_t size, js_vmem **out);
void *js_vmem_address(const js_vmem *region);
/* Result receives the Horizon Result code, or zero for local validation errors. */
js_vmem_status js_vmem_commit(js_vmem *region, uint32_t *result);
js_vmem_status js_vmem_uncommit(js_vmem *region, uint32_t *result);
/* On unmap failure, retains the region so release can be retried. */
js_vmem_status js_vmem_release(js_vmem *region, uint32_t *result);
#ifdef __cplusplus
}
#endif
#endif
