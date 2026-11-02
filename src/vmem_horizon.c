/* Experimental address reservation and physical page mappings, not mmap. */
#include <javaswitch/vmem.h>
#include <javaswitch/detail/horizon_alias.h>
#include <switch.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE ((size_t)4096)
struct js_vmem {
    void *address;
    size_t size;
    js_horizon_alias_reservation *reservation;
    /* Zero means unmapped; other values are js_vmem_access. */
    unsigned char *pages;
    struct js_vmem *next;
};
static Mutex vmem_lock;
static js_vmem *vmem_regions;

js_vmem_status js_vmem_reserve(size_t size, js_vmem **out) {
    return js_vmem_reserve_aligned(size, PAGE_SIZE, out);
}

js_vmem_status js_vmem_reserve_aligned(size_t size, size_t alignment, js_vmem **out) {
    if (!out) return JS_VMEM_INVALID;
    *out = NULL;
    if (!size || (size & (PAGE_SIZE - 1)) || alignment < PAGE_SIZE ||
        (alignment & (alignment - 1)) || alignment > SIZE_MAX - PAGE_SIZE ||
        size > SIZE_MAX - alignment - PAGE_SIZE)
        return JS_VMEM_INVALID;
    js_vmem *v = calloc(1, sizeof(*v));
    if (!v) return JS_VMEM_NO_MEMORY;
    v->pages = calloc(size / PAGE_SIZE, 1);
    if (!v->pages) { free(v); return JS_VMEM_NO_MEMORY; }
    js_horizon_alias_status alias_status = js_horizon_alias_reserve(
        size, alignment, &v->address, &v->reservation, NULL);
    js_vmem_status status = alias_status == JS_HORIZON_ALIAS_OK ? JS_VMEM_OK :
        alias_status == JS_HORIZON_ALIAS_NO_MEMORY ? JS_VMEM_NO_MEMORY :
        alias_status == JS_HORIZON_ALIAS_INVALID ? JS_VMEM_INVALID :
        alias_status == JS_HORIZON_ALIAS_STATE ? JS_VMEM_STATE : JS_VMEM_SYSTEM;
    if (status == JS_VMEM_OK) {
        mutexLock(&vmem_lock);
        v->size = size;
        v->next = vmem_regions;
        vmem_regions = v;
        mutexUnlock(&vmem_lock);
    }
    if (status != JS_VMEM_OK) { free(v->pages); free(v); return status; }
    *out = v;
    return JS_VMEM_OK;
}

void *js_vmem_address(const js_vmem *v) { return v ? v->address : NULL; }
size_t js_vmem_size(const js_vmem *v) { return v ? v->size : 0; }

static int valid_range(const js_vmem *v, size_t offset, size_t length) {
    return v && length && !((offset | length) & (PAGE_SIZE - 1)) &&
           offset < v->size && length <= v->size - offset;
}

static int all_mapped(const js_vmem *v, size_t offset, size_t length, int mapped) {
    size_t end = (offset + length) / PAGE_SIZE;
    for (size_t i = offset / PAGE_SIZE; i < end; i++)
        if ((v->pages[i] != 0) != mapped) return 0;
    return 1;
}

static js_vmem_status change_mapping(js_vmem *v, size_t offset, size_t length,
                                      int commit, uint32_t *result) {
    if (result) *result = 0;
    if (!valid_range(v, offset, length)) return JS_VMEM_INVALID;
    mutexLock(&vmem_lock);
    if (!all_mapped(v, offset, length, !commit)) {
        mutexUnlock(&vmem_lock);
        return JS_VMEM_STATE;
    }
    virtmemLock();
    void *address = (unsigned char *)v->address + offset;
    Result rc = commit ? svcMapPhysicalMemory(address, length)
                       : svcUnmapPhysicalMemory(address, length);
    virtmemUnlock();
    if (result) *result = rc;
    if (R_SUCCEEDED(rc))
        memset(v->pages + offset / PAGE_SIZE, commit ? JS_VMEM_READ_WRITE : 0, length / PAGE_SIZE);
    mutexUnlock(&vmem_lock);
    return R_SUCCEEDED(rc) ? JS_VMEM_OK : JS_VMEM_SYSTEM;
}

js_vmem_status js_vmem_commit_range(js_vmem *v, size_t offset, size_t length, uint32_t *result) {
    return change_mapping(v, offset, length, 1, result);
}

js_vmem_status js_vmem_uncommit_range(js_vmem *v, size_t offset, size_t length, uint32_t *result) {
    return change_mapping(v, offset, length, 0, result);
}

js_vmem_status js_vmem_commit(js_vmem *v, uint32_t *result) {
    return js_vmem_commit_range(v, 0, js_vmem_size(v), result);
}

js_vmem_status js_vmem_uncommit(js_vmem *v, uint32_t *result) {
    return js_vmem_uncommit_range(v, 0, js_vmem_size(v), result);
}

js_vmem_status js_vmem_protect_range(js_vmem *v, size_t offset, size_t length,
                                    js_vmem_access access, uint32_t *result) {
    if (result) *result = 0;
    if (!valid_range(v, offset, length)) return JS_VMEM_INVALID;
    if (access == JS_VMEM_READ_EXECUTE) return JS_VMEM_UNSUPPORTED;
    unsigned permission;
    switch (access) {
        case JS_VMEM_NONE: permission = Perm_None; break;
        case JS_VMEM_READ: permission = Perm_R; break;
        case JS_VMEM_READ_WRITE: permission = Perm_Rw; break;
        default: return JS_VMEM_INVALID;
    }
    mutexLock(&vmem_lock);
    if (!all_mapped(v, offset, length, 1)) {
        mutexUnlock(&vmem_lock);
        return JS_VMEM_STATE;
    }
    Result rc = svcSetMemoryPermission((unsigned char *)v->address + offset, length, permission);
    if (result) *result = rc;
    if (R_SUCCEEDED(rc)) memset(v->pages + offset / PAGE_SIZE, access, length / PAGE_SIZE);
    mutexUnlock(&vmem_lock);
    return R_SUCCEEDED(rc) ? JS_VMEM_OK : JS_VMEM_SYSTEM;
}

js_vmem_status js_vmem_release(js_vmem *v, uint32_t *result) {
    if (result) *result = 0;
    if (!v) return JS_VMEM_INVALID;
    mutexLock(&vmem_lock);
    virtmemLock();
    size_t count = v->size / PAGE_SIZE;
    for (size_t first = 0; first < count;) {
        if (!v->pages[first]) { first++; continue; }
        size_t end = first + 1;
        while (end < count && v->pages[end]) end++;
        Result rc = svcUnmapPhysicalMemory((unsigned char *)v->address + first * PAGE_SIZE,
                                           (end - first) * PAGE_SIZE);
        if (result) *result = rc;
        if (R_FAILED(rc)) {
            virtmemUnlock(); mutexUnlock(&vmem_lock);
            return JS_VMEM_SYSTEM;
        }
        memset(v->pages + first, 0, end - first);
        first = end;
    }
    virtmemUnlock();
    js_horizon_alias_status alias_status = js_horizon_alias_release(v->reservation, NULL);
    if (alias_status != JS_HORIZON_ALIAS_OK) {
        mutexUnlock(&vmem_lock);
        return alias_status == JS_HORIZON_ALIAS_NO_MEMORY ? JS_VMEM_NO_MEMORY : JS_VMEM_STATE;
    }
    js_vmem **link = &vmem_regions;
    while (*link != v) link = &(*link)->next;
    *link = v->next;
    free(v->pages);
    free(v);
    mutexUnlock(&vmem_lock);
    return JS_VMEM_OK;
}
