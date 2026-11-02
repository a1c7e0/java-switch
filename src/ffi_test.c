#include "platform.h"
#include <ffi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <javaswitch/vmem.h>
extern Result js_ffi_last_result(void);
static int add(int a,int b){return a+b;}
static double mixed(int a,double b,uint64_t c){return a+b+(double)c;}
static void upcall(ffi_cif *c,void *result,void **args,void *context){(void)c;*(int *)result=*(int *)args[0]+*(int *)context;}
static void downcalls(void){
    ffi_cif cif;ffi_type *types[]={&ffi_type_sint32,&ffi_type_sint32};int a=20,b=22;void *args[]={&a,&b};ffi_arg result=0;
    ffi_status s=ffi_prep_cif(&cif,FFI_DEFAULT_ABI,2,&ffi_type_sint32,types);
    if(s!=FFI_OK){sc_log("ffi_prep_cif=%d\n",s);return;}
    ffi_call(&cif,FFI_FN(add),&result,args);sc_log("AAPCS64 integer downcall=%ld expected=42 %s\n",(long)result,result==42?"PASS":"FAIL");
    ffi_type *mixed_types[]={&ffi_type_sint32,&ffi_type_double,&ffi_type_uint64};double f=1.5,out=0;uint64_t wide=20;void *mixed_args[]={&a,&f,&wide};
    s=ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_double,mixed_types);
    if(s!=FFI_OK){sc_log("ffi mixed cif=%d\n",s);return;}
    ffi_call(&cif,FFI_FN(mixed),&out,mixed_args);sc_log("AAPCS64 mixed integer/FP downcall=%.1f expected=41.5 %s\n",out,out==41.5?"PASS":"FAIL");
}
static void closure(void){
    ffi_cif cif;ffi_type *types[]={&ffi_type_sint32};int context=21;
    ffi_status s=ffi_prep_cif(&cif,FFI_DEFAULT_ABI,1,&ffi_type_sint32,types);if(s!=FFI_OK)return;
    void *code=NULL;ffi_closure *rw=ffi_closure_alloc(sizeof(*rw),&code);
    if(!rw){sc_log("ffi_closure_alloc unavailable: libnx Result=0x%x; no kernel modification attempted\n",js_ffi_last_result());return;}
    sc_log("libffi closure allocated using libnx JIT RW/RX permissions\n");
    s=ffi_prep_closure_loc(rw,&cif,upcall,&context,code);
    if(s==FFI_OK){int result=((int (*)(int))code)(21);sc_log("FFM-style upcall=%d expected=42 %s\n",result,result==42?"PASS":"FAIL");}
    else sc_log("ffi_prep_closure_loc=%d libnx Result=0x%x\n",s,js_ffi_last_result());
    ffi_closure_free(rw);sc_log("jitClose Result=0x%x\n",js_ffi_last_result());
}
static void memory_probe(void){
    js_vmem *region=NULL; uint32_t result=0;
    js_vmem_status status=js_vmem_reserve(65536,&region);
    if(status!=JS_VMEM_OK){sc_log("vmem reserve failed: status=%d\n",status);return;}
    sc_log("vmem reserved 65536 bytes at %p\n",js_vmem_address(region));
    status=js_vmem_commit(region,&result);
    if(status==JS_VMEM_OK){
        volatile unsigned char *p=js_vmem_address(region);
        p[0]=0x5a;p[65535]=0xa5;
        sc_log("vmem commit and readback %s\n",p[0]==0x5a&&p[65535]==0xa5?"PASS":"FAIL");
        status=js_vmem_uncommit(region,&result);
        sc_log("vmem uncommit status=%d Result=0x%x\n",status,result);
        if(status==JS_VMEM_OK){
            status=js_vmem_commit(region,&result);
            sc_log("vmem recommit status=%d Result=0x%x\n",status,result);
        }
    }else sc_log("vmem commit failed: status=%d Result=0x%x\n",status,result);
    status=js_vmem_release(region,&result);
    sc_log("vmem release status=%d Result=0x%x\n",status,result);
}
int main(void){sc_init("ffi-test");sc_log("libffi Horizon / Java 25 fallback linker prerequisites\nA: native downcalls | X: W^X upcall closure | +: exit\nThis does not initialize a Java 25 VM. Hardware NOT TESTED.\n");u64 keys;
    sc_log("Y: virtual memory reserve / commit / uncommit probe\n");
    while(sc_frame(&keys)){if(keys&HidNpadButton_A)downcalls();if(keys&HidNpadButton_X)closure();if(keys&HidNpadButton_Y)memory_probe();}sc_exit();return 0;
}
