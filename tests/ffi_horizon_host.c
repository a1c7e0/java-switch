#include <switch.h>
#include <ffi.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
extern Result js_ffi_last_result(void);
extern void *ffi_data_to_code_pointer(void *);
static int writable_calls, executable_calls, close_calls, prepare_calls;
static int fail_writable, fail_executable, check_busy;
Result jitCreate(Jit *j, size_t n) {
    j->size = n; j->rw_addr = calloc(1,n); j->rx_addr = calloc(1,n);
    assert(j->rw_addr && j->rx_addr); return 0;
}
Result jitTransitionToWritable(Jit *j) { (void)j; writable_calls++; return fail_writable ? 42 : 0; }
Result jitTransitionToExecutable(Jit *j) { (void)j; executable_calls++; return fail_executable ? 43 : 0; }
Result jitClose(Jit *j) { close_calls++; free(j->rw_addr); free(j->rx_addr); return 0; }
static void callback(ffi_cif *c, void *r, void **a, void *d) { (void)c;(void)r;(void)a;(void)d; }
ffi_status js_ffi_prep_closure_impl(ffi_closure *c, ffi_cif *cif,
    void (*fun)(ffi_cif *,void *,void **,void *), void *data, void *code) {
    (void)cif; (void)fun; (void)data; prepare_calls++;
    assert(ffi_data_to_code_pointer((char *)c + 8) == (char *)code + 8);
    if (check_busy) { errno=0; ffi_closure_free(c); assert(errno==EBUSY); assert(close_calls==0); }
    return FFI_OK;
}
int main(void) {
    void *code=(void *)1; ffi_cif cif;
    errno=0; assert(ffi_closure_alloc(0,&code)==NULL && errno==EINVAL);
    ffi_closure *c=ffi_closure_alloc(sizeof(*c),&code); assert(c && code);
    assert(writable_calls==1);
    errno=0; assert(ffi_prep_closure_loc(c,&cif,callback,NULL,(char *)code+1)==FFI_BAD_ABI);
    assert(errno==EINVAL && prepare_calls==0);
    check_busy=1; assert(ffi_prep_closure_loc(c,&cif,callback,NULL,code)==FFI_OK);
    assert(executable_calls==1 && close_calls==0);
    check_busy=0; fail_writable=1;
    assert(ffi_prep_closure_loc(c,&cif,callback,NULL,code)==FFI_BAD_ABI);
    assert(js_ffi_last_result()==42 && prepare_calls==1);
    fail_writable=0; fail_executable=1;
    assert(ffi_prep_closure_loc(c,&cif,callback,NULL,code)==FFI_BAD_ABI);
    assert(js_ffi_last_result()==43 && prepare_calls==2);
    int previous=writable_calls; fail_executable=0;
    assert(ffi_prep_closure_loc(c,&cif,callback,NULL,code)==FFI_OK);
    assert(writable_calls==previous);
    assert(ffi_prep_closure_loc(c,&cif,callback,NULL,code)==FFI_OK);
    assert(writable_calls==previous+1);
    ffi_closure_free(c); assert(close_calls==1 && js_ffi_last_result()==0);
    puts("FFI closure lifecycle and injected failures: PASS");
    return 0;
}
