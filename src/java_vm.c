/* miniJVM headers use integer typedefs that differ from libnx. Keep this
 * translation unit independent of switch.h; java_test.c owns Horizon services. */
#include "jvm.h"
#include "garbage.h"
#include <stdarg.h>
#include <stdio.h>
extern void sc_log(const char *, ...);

static s32 add(Runtime *runtime,JClass *clazz) {
    (void)clazz;
    s32 a=localvar_getInt(runtime->localvar,0),b=localvar_getInt(runtime->localvar,1);
    push_int(runtime->stack,a+b);return 0;
}
int sc_java_run(const char *root,int user_test) {
    char boot[512],cp[1024],instance[512];
    snprintf(boot,sizeof(boot),"%s/runtimes/minijvm/minijvm_rt.jar",root);
    snprintf(cp,sizeof(cp),"%s/tests/%s;%s/tests/dependency.jar",root,user_test?"user.jar":"diagnostic-tests.jar",root);
    snprintf(instance,sizeof(instance),"%s/instances/java-smoke",root);
    FILE *f=fopen(boot,"rb");if(!f){sc_log("Missing class library: %s\n",boot);return -1;}fclose(f);
    jvm_init_mem_alloc();
    MiniJVM *jvm=jvm_create();if(!jvm){sc_log("jvm_create FAILED\n");return -1;}
    jvm->gc_backend="malloc";jvm->max_heap_size=64*1024*1024;
    jvm->max_vm_memory=128*1024*1024;jvm->jdwp_enable=0;
    jvm->startup_dir=utf8_create_c(instance);
    sc_log("Initializing miniJVM interpreter, classic GC, Java 8 class library\n");
    int rc=jvm_init(jvm,boot,cp);
    sc_log("jvm_init -> %d\n",rc);
    if(!rc) {
        static java_native_method bridge[]={ {"DiagnosticSmoke","nativeAdd","(II)I",add} };
        native_reg_lib(jvm,bridge,1);
        ArrayList *args=arraylist_create(2);arraylist_push_back(args,instance);arraylist_push_back(args,"--native");
        rc=call_main(jvm,user_test?"UserMain":"DiagnosticSmoke",args);arraylist_destroy(args);
        sc_log("call_main -> %d\n",rc);
    }
    sc_log("JVM teardown begins\n");jvm_destroy(jvm);jvm_destroy_mem_alloc();
    sc_log("JVM teardown completed\n");return rc;
}
void sc_java_write_byte(unsigned char value) {sc_log("%c",value);}
int sc_java_vprintf(const char *format,va_list args) {
    char text[2048];int size=vsnprintf(text,sizeof(text),format,args);sc_log("%s",text);return size;
}
