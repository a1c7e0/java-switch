#include <javaswitch/vmem.h>
#include <switch.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
struct VirtmemReservation { void *p; };
static int locked, map_calls, unmap_calls, remove_calls, map_fail, unmap_fail;
void virtmemLock(void) { assert(!locked); locked=1; }
void virtmemUnlock(void) { assert(locked); locked=0; }
void *virtmemFindAslr(size_t size, size_t guard) { assert(locked && guard==4096); return malloc(size); }
VirtmemReservation *virtmemAddReservation(void *p, size_t size) {
    (void)size; assert(locked); VirtmemReservation *r=malloc(sizeof(*r)); assert(r); r->p=p; return r;
}
void virtmemRemoveReservation(VirtmemReservation *r) { assert(locked); remove_calls++; free(r->p); free(r); }
Result svcMapPhysicalMemory(void *p, size_t size) { assert(locked && p && size==8192); map_calls++; return map_fail?44:0; }
Result svcUnmapPhysicalMemory(void *p, size_t size) { assert(locked && p && size==8192); unmap_calls++; return unmap_fail?45:0; }
int main(void) {
    js_vmem *v=NULL; uint32_t rc=999;
    assert(js_vmem_reserve(0,&v)==JS_VMEM_INVALID);
    assert(js_vmem_reserve(8193,&v)==JS_VMEM_INVALID);
    assert(js_vmem_reserve(SIZE_MAX & ~(size_t)0xfff,&v)==JS_VMEM_INVALID);
    assert(js_vmem_reserve(8192,&v)==JS_VMEM_OK && js_vmem_address(v));
    assert(js_vmem_uncommit(v,&rc)==JS_VMEM_STATE && rc==0);
    map_fail=1; assert(js_vmem_commit(v,&rc)==JS_VMEM_SYSTEM && rc==44);
    map_fail=0; assert(js_vmem_commit(v,&rc)==JS_VMEM_OK && rc==0);
    assert(js_vmem_commit(v,&rc)==JS_VMEM_STATE && map_calls==2);
    unmap_fail=1; assert(js_vmem_uncommit(v,&rc)==JS_VMEM_SYSTEM && rc==45);
    assert(js_vmem_release(v,&rc)==JS_VMEM_SYSTEM && rc==45 && remove_calls==0);
    unmap_fail=0; assert(js_vmem_uncommit(v,&rc)==JS_VMEM_OK);
    assert(js_vmem_commit(v,&rc)==JS_VMEM_OK);
    assert(js_vmem_release(v,&rc)==JS_VMEM_OK && remove_calls==1 && unmap_calls==4);
    puts("vmem state and injected failures: PASS");
    return 0;
}
