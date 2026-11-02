/* Real libnx W^X allocation for libffi upcall closures. */
#include <switch.h>
#include <ffi.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
typedef struct Allocation {Jit jit;struct Allocation *next;} Allocation;
static Allocation *allocations;
static Mutex lock;
static Result last_result;
Result js_ffi_last_result(void){return last_result;}
void *ffi_closure_alloc(size_t size,void **code){
    if(!code||!size||size>SIZE_MAX-0xfff){errno=EINVAL;return NULL;}
    Allocation *a=calloc(1,sizeof(*a));if(!a){errno=ENOMEM;return NULL;}
    size_t aligned=(size+0xfff)&~(size_t)0xfff;
    last_result=jitCreate(&a->jit,aligned);
    if(R_FAILED(last_result)){free(a);errno=ENOTSUP;return NULL;}
    last_result=jitTransitionToWritable(&a->jit);
    if(R_FAILED(last_result)){jitClose(&a->jit);free(a);errno=ENOTSUP;return NULL;}
    *code=jitGetRxAddr(&a->jit);
    mutexLock(&lock);a->next=allocations;allocations=a;mutexUnlock(&lock);
    return jitGetRwAddr(&a->jit);
}
void *ffi_data_to_code_pointer(void *data){
    uintptr_t ptr=(uintptr_t)data;void *result=data;
    mutexLock(&lock);
    for(Allocation *a=allocations;a;a=a->next){uintptr_t start=(uintptr_t)jitGetRwAddr(&a->jit);if(ptr>=start&&ptr-start<a->jit.size){result=(char *)jitGetRxAddr(&a->jit)+(ptr-start);break;}}
    mutexUnlock(&lock);return result;
}
void ffi_closure_free(void *closure){
    mutexLock(&lock);Allocation **link=&allocations;
    while(*link){Allocation *a=*link;if(closure==jitGetRwAddr(&a->jit)||closure==jitGetRxAddr(&a->jit)){
        last_result=jitClose(&a->jit);if(R_SUCCEEDED(last_result)){*link=a->next;free(a);}break;
    }link=&a->next;}
    mutexUnlock(&lock);
}
extern ffi_status js_ffi_prep_closure_impl(ffi_closure *,ffi_cif *,void (*)(ffi_cif *,void *,void **,void *),void *,void *);
ffi_status ffi_prep_closure_loc(ffi_closure *closure,ffi_cif *cif,void (*fun)(ffi_cif *,void *,void **,void *),void *data,void *code){
    Allocation *owner=NULL;
    mutexLock(&lock);for(Allocation *a=allocations;a;a=a->next)if(closure==jitGetRwAddr(&a->jit)){owner=a;break;}mutexUnlock(&lock);
    if(!owner)return FFI_BAD_ABI;
    ffi_status status=js_ffi_prep_closure_impl(closure,cif,fun,data,code);
    if(status!=FFI_OK)return status;
    last_result=jitTransitionToExecutable(&owner->jit);
    return R_SUCCEEDED(last_result)?FFI_OK:FFI_BAD_ABI;
}
