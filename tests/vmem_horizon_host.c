#include <javaswitch/vmem.h>
#include <switch.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ALIAS_BASE ((uintptr_t)0x10000000)
#define ALIAS_SIZE ((size_t)(256 * 4096))
#define ALIAS_EXTRA_SIZE ((size_t)(2 * 4096))
#define ALIAS_PAGES (ALIAS_SIZE / 4096)
#define MAX_RESERVATIONS 128
struct VirtmemReservation { uintptr_t p; size_t size; int active; };
static int locked, map_calls, unmap_calls, protect_calls, remove_calls;
static int map_fail, unmap_fail, protect_fail, unmap_fail_at, add_fail, alias_full;
static Result map_failure_result=44, unmap_failure_result=45;
static int calloc_fail_at, calloc_calls;
static VirtmemReservation reservations[MAX_RESERVATIONS];
static unsigned char mapped[ALIAS_PAGES];
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size) {
    if (++calloc_calls == calloc_fail_at) return NULL;
    return __real_calloc(count, size);
}
void virtmemLock(void) { assert(!locked); locked=1; }
void virtmemUnlock(void) { assert(locked); locked=0; }
void *virtmemFindAslr(size_t size, size_t guard) {
    (void)size; (void)guard;
    assert(!"svcMapPhysicalMemory allocator must not use ASLR finder");
    return NULL;
}
VirtmemReservation *virtmemAddReservation(void *p, size_t size) {
    uintptr_t address=(uintptr_t)p;
    assert(locked && address>=ALIAS_BASE && address<=ALIAS_BASE+ALIAS_SIZE);
    assert(size && size<=ALIAS_BASE+ALIAS_SIZE-address);
    if (add_fail) return NULL;
    for (size_t i=0;i<MAX_RESERVATIONS;i++) if (!reservations[i].active) {
        reservations[i]=(VirtmemReservation){address,size,1};
        return &reservations[i];
    }
    return NULL;
}
void virtmemRemoveReservation(VirtmemReservation *r) {
    assert(locked && r && r->active);
    remove_calls++; r->active=0;
}
static size_t page_index(void *p, size_t size) {
    uintptr_t address=(uintptr_t)p;
    assert(address>=ALIAS_BASE && address<=ALIAS_BASE+ALIAS_SIZE);
    assert(!(address&4095) && size && !(size&4095));
    assert(size<=ALIAS_BASE+ALIAS_SIZE-address);
    size_t first=(address-ALIAS_BASE)/4096;
    assert(first+size/4096<=ALIAS_PAGES);
    return first;
}
Result svcGetInfo(u64 *out, unsigned id, uint64_t handle, uint64_t sub_id) {
    assert(handle==CUR_PROCESS_HANDLE && sub_id==0);
    if (id==InfoType_AliasRegionAddress) *out=ALIAS_BASE;
    else if (id==InfoType_AliasRegionSize) *out=ALIAS_SIZE;
    else if (id==InfoType_AliasRegionExtraSize) *out=ALIAS_EXTRA_SIZE;
    else return 88;
    return 0;
}
Result svcQueryMemory(MemoryInfo *info, u32 *pageinfo, u64 address) {
    assert(address>=ALIAS_BASE && address<ALIAS_BASE+ALIAS_SIZE);
    size_t page=(size_t)((address-ALIAS_BASE)/4096);
    size_t first=page, last=page+1;
    while (first>0 && mapped[first-1]==mapped[page]) first--;
    while (last<ALIAS_PAGES && mapped[last]==mapped[page]) last++;
    info->addr=ALIAS_BASE+first*4096;
    info->size=(last-first)*4096;
    info->type=(mapped[page] || alias_full) ? 2u : MemType_Unmapped;
    info->attr=info->perm=info->ipc_refcount=info->device_refcount=info->padding=0;
    *pageinfo=0;
    return 0;
}
Result svcMapPhysicalMemory(void *p, size_t size) {
    assert(locked); map_calls++;
    size_t first=page_index(p,size);
    for (size_t i=first;i<first+size/4096;i++) assert(!mapped[i]);
    if (map_fail) return map_failure_result;
    memset(mapped+first,Perm_Rw,size/4096); return 0;
}
Result svcUnmapPhysicalMemory(void *p, size_t size) {
    assert(locked); unmap_calls++;
    size_t first=page_index(p,size);
    for (size_t i=first;i<first+size/4096;i++) assert(mapped[i]);
    if (unmap_fail || unmap_calls==unmap_fail_at) return unmap_failure_result;
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
    assert((uintptr_t)js_vmem_address(v)+js_vmem_size(v) <=
           ALIAS_BASE+ALIAS_SIZE-ALIAS_EXTRA_SIZE);
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
    for (int allocation=1;allocation<=3;allocation++) {
        calloc_fail_at=calloc_calls+allocation;
        assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v);
    }
    calloc_fail_at=0;
    alias_full=1; assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v); alias_full=0;
    add_fail=1; assert(js_vmem_reserve(8192,&v)==JS_VMEM_NO_MEMORY && !v); add_fail=0;
}
static void ranges_and_protection(void) {
    js_vmem *v=NULL; uint32_t rc=999;
    assert(js_vmem_reserve_aligned(16384,65536,&v)==JS_VMEM_OK);
    assert(!((uintptr_t)js_vmem_address(v)&65535) && js_vmem_size(v)==16384);
    assert((uintptr_t)js_vmem_address(v)+js_vmem_size(v) <=
           ALIAS_BASE+ALIAS_SIZE-ALIAS_EXTRA_SIZE);
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
static void system_resource_failure(void) {
    js_vmem *v=NULL; uint32_t rc=0;
    assert(js_vmem_reserve(8192,&v)==JS_VMEM_OK);
    map_fail=1; map_failure_result=0xfa01;
    assert(js_vmem_commit_range(v,0,4096,&rc)==JS_VMEM_UNSUPPORTED && rc==0xfa01);
    map_fail=0; map_failure_result=44;
    assert(js_vmem_commit_range(v,0,4096,&rc)==JS_VMEM_OK);
    unmap_fail=1; unmap_failure_result=0xfa01;
    assert(js_vmem_uncommit_range(v,0,4096,&rc)==JS_VMEM_UNSUPPORTED && rc==0xfa01);
    unmap_fail=0; unmap_failure_result=45;
    assert(js_vmem_release(v,&rc)==JS_VMEM_OK && rc==0);
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
    allocation_failures();
    reserve_failures(); ranges_and_protection(); release_retry();
    system_resource_failure(); concurrent_commit();
    assert(!locked);
    for (size_t i=0;i<MAX_RESERVATIONS;i++) assert(!reservations[i].active);
    puts("vmem state and injected failures: PASS");
    puts("partial mappings, alignment, protection, release retry, concurrent commit: PASS");
    return 0;
}
