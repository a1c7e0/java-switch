#include "javaswitch/runtime.h"
#include "jvm.h"
#include "jvm_util.h"
int js_minijvm_launch(const JsLaunchRequest *q,JsLaunchResult *out) {
    if(!q->boot_classpath||q->on_vm_ready){out->code=JS_BAD_ARGUMENT;return out->code;}
    jvm_init_mem_alloc();MiniJVM *vm=jvm_create();
    out->stage="minijvm-create";
    if(!vm){out->code=JS_UNAVAILABLE;return out->code;}
    vm->gc_backend="malloc";vm->max_heap_size=(s64)q->heap_mib*1024*1024;
    vm->max_vm_memory=vm->max_heap_size*2;vm->jdwp_enable=0;
    out->stage="minijvm-init";int rc=jvm_init(vm,(char *)q->boot_classpath,(char *)q->classpath);
    if(!rc){
        ArrayList *args=arraylist_create((s32)q->argument_count);
        for(size_t i=0;i<q->argument_count;i++)arraylist_push_back(args,(void *)q->arguments[i]);
        out->stage="minijvm-main";rc=call_main(vm,(char *)q->main_class,args);arraylist_destroy(args);
    }
    jvm_destroy(vm);jvm_destroy_mem_alloc();out->code=rc?JS_JAVA_EXCEPTION:JS_OK;
    if(!rc)out->stage="completed";
    return out->code;
}
