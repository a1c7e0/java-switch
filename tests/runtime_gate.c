#include "javaswitch/runtime.h"
#include <stdio.h>
#include <string.h>
int main(void){
    JsRuntimeInfo info;JsLaunchResult out;
    if(js_runtime_query("openjdk25",&info)!=JS_OK||info.available)return 1;
    JsLaunchRequest q={.abi_version=JS_ABI_VERSION,.required_java_major=25,.backend="openjdk25",.classpath="",.main_class="Main",.heap_mib=64};
    if(js_runtime_launch(&q,&out)!=JS_UNAVAILABLE||strcmp(out.stage,"runtime-not-linked"))return 2;
    q.backend="minijvm8";
    if(js_runtime_launch(&q,&out)!=JS_INCOMPATIBLE||strcmp(out.stage,"java-version-mismatch"))return 3;
    q.abi_version=99;if(js_runtime_launch(&q,&out)!=JS_BAD_ARGUMENT)return 4;
    puts("RUNTIME GATES PASS");return 0;
}
