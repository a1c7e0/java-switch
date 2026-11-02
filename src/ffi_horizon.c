/* libnx W^X allocation for libffi upcall closures. */
#include <switch.h>
#include <ffi.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct Allocation {
    Jit jit;
    struct Allocation *next;
    int preparing;
    int executable;
} Allocation;
static Allocation *allocations;
static Mutex lock;
static _Thread_local Result last_result;
Result js_ffi_last_result(void) { return last_result; }

void *ffi_closure_alloc(size_t size, void **code) {
    last_result = 0;
    if (!code || !size || size > SIZE_MAX - 0xfff) { errno = EINVAL; return NULL; }
    *code = NULL;
    Allocation *a = calloc(1, sizeof(*a));
    if (!a) { errno = ENOMEM; return NULL; }
    size_t aligned = (size + 0xfff) & ~(size_t)0xfff;
    last_result = jitCreate(&a->jit, aligned);
    if (R_FAILED(last_result)) { free(a); errno = ENOTSUP; return NULL; }
    last_result = jitTransitionToWritable(&a->jit);
    if (R_FAILED(last_result)) { jitClose(&a->jit); free(a); errno = ENOTSUP; return NULL; }
    *code = jitGetRxAddr(&a->jit);
    mutexLock(&lock); a->next = allocations; allocations = a; mutexUnlock(&lock);
    return jitGetRwAddr(&a->jit);
}

void *ffi_data_to_code_pointer(void *data) {
    uintptr_t ptr = (uintptr_t)data;
    void *result = data;
    mutexLock(&lock);
    for (Allocation *a = allocations; a; a = a->next) {
        uintptr_t start = (uintptr_t)jitGetRwAddr(&a->jit);
        if (ptr >= start && ptr - start < a->jit.size) {
            result = (char *)jitGetRxAddr(&a->jit) + (ptr - start);
            break;
        }
    }
    mutexUnlock(&lock);
    return result;
}

void ffi_closure_free(void *closure) {
    last_result = 0;
    if (!closure) return;
    mutexLock(&lock);
    Allocation **link = &allocations;
    while (*link) {
        Allocation *a = *link;
        if (closure == jitGetRwAddr(&a->jit) || closure == jitGetRxAddr(&a->jit)) {
            if (a->preparing) { errno = EBUSY; mutexUnlock(&lock); return; }
            last_result = jitClose(&a->jit);
            if (R_SUCCEEDED(last_result)) { *link = a->next; free(a); }
            else errno = EIO;
            mutexUnlock(&lock);
            return;
        }
        link = &a->next;
    }
    mutexUnlock(&lock);
    errno = EINVAL;
}

extern ffi_status js_ffi_prep_closure_impl(ffi_closure *, ffi_cif *,
    void (*)(ffi_cif *, void *, void **, void *), void *, void *);
ffi_status ffi_prep_closure_loc(ffi_closure *closure, ffi_cif *cif,
    void (*fun)(ffi_cif *, void *, void **, void *), void *data, void *code) {
    last_result = 0;
    if (!closure || !cif || !fun || !code) { errno = EINVAL; return FFI_BAD_ABI; }
    mutexLock(&lock);
    Allocation *owner = allocations;
    while (owner && closure != jitGetRwAddr(&owner->jit)) owner = owner->next;
    if (!owner || code != jitGetRxAddr(&owner->jit)) {
        mutexUnlock(&lock); errno = EINVAL; return FFI_BAD_ABI;
    }
    if (owner->preparing) { mutexUnlock(&lock); errno = EBUSY; return FFI_BAD_ABI; }
    owner->preparing = 1;
    int was_executable = owner->executable;
    mutexUnlock(&lock);

    ffi_status status = FFI_BAD_ABI;
    if (was_executable) {
        last_result = jitTransitionToWritable(&owner->jit);
        if (R_FAILED(last_result)) { errno = ENOTSUP; goto done; }
        owner->executable = 0;
    }
    status = js_ffi_prep_closure_impl(closure, cif, fun, data, code);
    if (status == FFI_OK) {
        last_result = jitTransitionToExecutable(&owner->jit);
        if (R_FAILED(last_result)) { errno = ENOTSUP; status = FFI_BAD_ABI; }
        else owner->executable = 1;
    }
done:
    mutexLock(&lock); owner->preparing = 0; mutexUnlock(&lock);
    return status;
}
