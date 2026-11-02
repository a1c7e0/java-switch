/* Whole-region reserve/commit experiment using libnx, no fake mmap semantics. */
#include <javaswitch/vmem.h>
#include <switch.h>
#include <stdlib.h>

struct js_vmem { void *address; size_t size; VirtmemReservation *reservation; int committed; };
static Mutex vmem_lock;

js_vmem_status js_vmem_reserve(size_t size, js_vmem **out) {
    if (!out || !size || (size & 0xfff) || size > SIZE_MAX - 0x2000)
        return JS_VMEM_INVALID;
    *out = NULL;
    js_vmem *v = calloc(1, sizeof(*v));
    if (!v) return JS_VMEM_NO_MEMORY;
    virtmemLock();
    v->address = virtmemFindAslr(size, 0x1000);
    if (v->address) v->reservation = virtmemAddReservation(v->address, size);
    virtmemUnlock();
    if (!v->reservation) { free(v); return JS_VMEM_NO_MEMORY; }
    v->size = size; *out = v;
    return JS_VMEM_OK;
}
void *js_vmem_address(const js_vmem *v) { return v ? v->address : NULL; }
js_vmem_status js_vmem_commit(js_vmem *v, uint32_t *result) {
    if (result) *result = 0;
    if (!v) return JS_VMEM_INVALID;
    mutexLock(&vmem_lock);
    if (v->committed) { mutexUnlock(&vmem_lock); return JS_VMEM_STATE; }
    virtmemLock();
    Result rc = svcMapPhysicalMemory(v->address, v->size);
    virtmemUnlock();
    if (result) *result = rc;
    if (R_SUCCEEDED(rc)) v->committed = 1;
    mutexUnlock(&vmem_lock);
    return R_SUCCEEDED(rc) ? JS_VMEM_OK : JS_VMEM_SYSTEM;
}
js_vmem_status js_vmem_uncommit(js_vmem *v, uint32_t *result) {
    if (result) *result = 0;
    if (!v) return JS_VMEM_INVALID;
    mutexLock(&vmem_lock);
    if (!v->committed) { mutexUnlock(&vmem_lock); return JS_VMEM_STATE; }
    virtmemLock();
    Result rc = svcUnmapPhysicalMemory(v->address, v->size);
    virtmemUnlock();
    if (result) *result = rc;
    if (R_SUCCEEDED(rc)) v->committed = 0;
    mutexUnlock(&vmem_lock);
    return R_SUCCEEDED(rc) ? JS_VMEM_OK : JS_VMEM_SYSTEM;
}
js_vmem_status js_vmem_release(js_vmem *v, uint32_t *result) {
    if (result) *result = 0;
    if (!v) return JS_VMEM_INVALID;
    mutexLock(&vmem_lock);
    virtmemLock();
    if (v->committed) {
        Result rc = svcUnmapPhysicalMemory(v->address, v->size);
        if (result) *result = rc;
        if (R_FAILED(rc)) { virtmemUnlock(); mutexUnlock(&vmem_lock); return JS_VMEM_SYSTEM; }
    }
    virtmemRemoveReservation(v->reservation);
    virtmemUnlock();
    free(v); mutexUnlock(&vmem_lock);
    return JS_VMEM_OK;
}
