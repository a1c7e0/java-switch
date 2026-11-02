#include <javaswitch/vmem.h>
#include <switch.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct VirtmemReservation { unsigned char *p; size_t size; };
static int locked, map_calls, unmap_calls, protect_calls, remove_calls;
static int map_fail, unmap_fail, protect_fail, unmap_fail_at, find_fail, add_fail;
static int calloc_fail_at, calloc_calls;
static VirtmemReservation active;
static unsigned char mapped[64];
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size) {
    if (++calloc_calls == calloc_fail_at) return NULL;
    return __real_calloc(count, size);
}
void virtmemLock(void) { assert(!locked); locked=1; }
void virtmemUnlock(void) { assert(locked); locked=0; }
void *virtmemFindAslr(size_t size, size_t guard) {
    assert(locked && guard==4096 && !active.p);
    if (find_fail) return NULL;
    active.p=aligned_alloc(4096,size); assert(active.p);
    active.size=size; memset(mapped,0,sizeof(mapped));
    return active.p;
}
VirtmemReservation *virtmemAddReservation(void *p, size_t size) {
    assert(locked && p==active.p && size==active.size);
    if (add_fail) { free(active.p); active.p=NULL; return NULL; }
    return &active;
}
void virtmemRemoveReservation(VirtmemReservation *r) {
    assert(locked && r==&active);
    for (size_t i=0;i<sizeof(mapped);i++) assert(!mapped[i]);
    remove_calls++; free(r->p); r->p=NULL;
}
static size_t page_index(void *p, size_t size) {
    uintptr_t address=(uintptr_t)p, base=(uintptr_t)active.p;
    assert(active.p && address>=base && !(address&4095) && size && !(size&4095));
    assert(address-base<=active.size && size<=active.size-(address-base));
    size_t first=(address-base)/4096;
    assert(first+size/4096<=sizeof(mapped));
    return first;
}
Result svcMapPhysicalMemory(void *p, size_t size) {
    assert(locked); map_calls++;
    size_t first=page_index(p,size);
    for (size_t i=first;i<first+size/4096;i++) assert(!mapped[i]);
    if (map_fail) return 44;
    memset(mapped+first,Perm_Rw,size/4096); return 0;
}
Result svcUnmapPhysicalMemory(void *p, size_t size) {
    assert(locked); unmap_calls++;
    size_t first=page_index(p,size);
    for (size_t i=first;i<first+size/4096;i++) assert(mapped[i]);
    if (unmap_fail || unmap_calls==unmap_fail_at) return 45;
    memset(mapped+first,0,size/4096); return 0;
}
Result svcSetMemoryPermission(void *p, size_t size, unsigned permission) {
    protect_calls++; size_t first=page_index(p,size);
    for (size_t i=first;i<first+size/4096;i++) assert(mapped[i]);
    assert(permission==Perm_None || permission==Perm_R || permission==Perm_Rw);
    if (protect_fail) return 46;
    /* Mock tracks mappedness separately from permission; NONE remains mapped. */
    return 0;
}
static void whole_region(void) {
    js_vmem *v=NULL; uint32_t rc=999;
    assert(js_vmem_reserve(0,&v)==JS_VMEM_INVALID && !v);
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
}
static void allocation_failures(void) {
    js_vmem *v=(void *)1;
    assert(js_vmem_reserve_aligned(8192,4095,&v)==JS_VMEM_INVALID && !v);
    assert(js_vmem_reserve_aligned(8192,12288,&v)==JS_VMEM_INVALID);
    assert(js_vmem_reserve_aligned(8192,(SIZE_MAX/2)+1,&v)==JS_VMEM_NO_MEMORY);
    /* Huge alignment above safely fails in the allocator; do not let the mock allocate it. */
}
static void reserve_failures(void) {
    js_vmem *v=(void *)1;
    for (int allocation=1;allocation<=2;allocation++) {
        calloc_fail_at=calloc_calls+allocation;
        assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v);
    }
    calloc_fail_at=0;
    find_fail=1; assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v); find_fail=0;
    add_fail=1; assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v); add_fail=0;
}
static void ranges_and_protection(void) {
    js_vmem *v=NULL; uint32_t rc=999;
    assert(js_vmem_reserve_aligned(16384,65536,&v)==JS_VMEM_OK);
    assert(!((uintptr_t)js_vmem_address(v)&65535) && js_vmem_size(v)==16384);
    int before=map_calls;
    assert(js_vmem_commit_range(v,1,4096,&rc)==JS_VMEM_INVALID && rc==0);
    assert(js_vmem_commit_range(v,0,0,&rc)==JS_VMEM_INVALID);
    assert(js_vmem_commit_range(v,16384,4096,&rc)==JS_VMEM_INVALID);
    assert(js_vmem_commit_range(v,4096,SIZE_MAX-4095,&rc)==JS_VMEM_INVALID);
    assert(map_calls==before);
    map_fail=1; assert(js_vmem_commit_range(v,4096,8192,&rc)==JS_VMEM_SYSTEM && rc==44);
    map_fail=0; assert(js_vmem_commit_range(v,4096,8192,&rc)==JS_VMEM_OK);
    before=map_calls;
    assert(js_vmem_commit(v,&rc)==JS_VMEM_STATE && map_calls==before);
    assert(js_vmem_uncommit(v,&rc)==JS_VMEM_STATE);
    assert(js_vmem_protect_range(v,0,4096,JS_VMEM_READ,&rc)==JS_VMEM_STATE);
    before=protect_calls;
    assert(js_vmem_protect_range(v,4096,8192,JS_VMEM_READ_EXECUTE,&rc)==JS_VMEM_UNSUPPORTED && rc==0);
    assert(js_vmem_protect_range(v,4096,8192,(js_vmem_access)99,&rc)==JS_VMEM_INVALID);
    assert(protect_calls==before);
    protect_fail=1;
    assert(js_vmem_protect_range(v,4096,8192,JS_VMEM_READ,&rc)==JS_VMEM_SYSTEM && rc==46);
    protect_fail=0;
    assert(js_vmem_protect_range(v,4096,8192,JS_VMEM_NONE,&rc)==JS_VMEM_OK);
    assert(js_vmem_protect_range(v,4096,8192,JS_VMEM_READ,&rc)==JS_VMEM_OK);
    assert(js_vmem_protect_range(v,4096,8192,JS_VMEM_READ_WRITE,&rc)==JS_VMEM_OK);
    unmap_fail=1; assert(js_vmem_uncommit_range(v,4096,4096,&rc)==JS_VMEM_SYSTEM && rc==45);
    unmap_fail=0; assert(js_vmem_uncommit_range(v,4096,4096,&rc)==JS_VMEM_OK);
    assert(js_vmem_commit_range(v,4096,4096,&rc)==JS_VMEM_OK);
    assert(js_vmem_release(v,&rc)==JS_VMEM_OK);
}
static void release_retry(void) {
    js_vmem *v=NULL; uint32_t rc;
    assert(js_vmem_reserve(16384,&v)==JS_VMEM_OK);
    assert(js_vmem_commit_range(v,0,4096,&rc)==JS_VMEM_OK);
    assert(js_vmem_commit_range(v,8192,4096,&rc)==JS_VMEM_OK);
    unmap_fail_at=unmap_calls+2;
    int before=remove_calls;
    assert(js_vmem_release(v,&rc)==JS_VMEM_SYSTEM && rc==45 && remove_calls==before);
    /* First run was unmapped before failure, and can now be committed again. */
    assert(js_vmem_commit_range(v,0,4096,&rc)==JS_VMEM_OK);
    assert(js_vmem_commit_range(v,8192,4096,&rc)==JS_VMEM_STATE);
    unmap_fail_at=0;
    assert(js_vmem_release(v,&rc)==JS_VMEM_OK && remove_calls==before+1);
}
struct Worker { js_vmem *v; js_vmem_status status; };
static void *commit_worker(void *arg) {
    struct Worker *w=arg;
    w->status=js_vmem_commit_range(w->v,0,4096,NULL);
    return NULL;
}
static void concurrent_commit(void) {
    js_vmem *v=NULL; assert(js_vmem_reserve(8192,&v)==JS_VMEM_OK);
    struct Worker a={.v=v},b={.v=v}; pthread_t ta,tb;
    int before=map_calls;
    assert(!pthread_create(&ta,NULL,commit_worker,&a));
    assert(!pthread_create(&tb,NULL,commit_worker,&b));
    assert(!pthread_join(ta,NULL) && !pthread_join(tb,NULL));
    assert((a.status==JS_VMEM_OK && b.status==JS_VMEM_STATE) ||
           (a.status==JS_VMEM_STATE && b.status==JS_VMEM_OK));
    assert(map_calls==before+1);
    assert(js_vmem_release(v,NULL)==JS_VMEM_OK);
}
int main(void) {
    whole_region();
    /* Validation tests must never request a huge real mock allocation. */
    find_fail=1; allocation_failures(); find_fail=0;
    reserve_failures(); ranges_and_protection(); release_retry(); concurrent_commit();
    assert(!active.p && !locked);
    puts("vmem state and injected failures: PASS");
    puts("partial mappings, alignment, protection, release retry, concurrent commit: PASS");
    return 0;
}
