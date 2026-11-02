#include "javaswitch/runtime.h"
#include <jni.h>
#include <stdio.h>
static jint add(JNIEnv *e,jclass c,jint a,jint b){(void)e;(void)c;return a+b;}
static int ready(void *v,void *e,void *c){
    (void)v;(void)c;JNIEnv *env=e;
    jclass cls=(*env)->FindClass(env,"org/javaswitch/test/Java25Smoke");if(!cls)return -1;
    JNINativeMethod methods[]={{"nativeAdd","(II)I",(void *)add}};
    return (*env)->RegisterNatives(env,cls,methods,1);
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const char *args[]={argv[2]};
    JsLaunchRequest q={.abi_version=JS_ABI_VERSION,.required_java_major=25,.backend="openjdk25",.classpath=argv[1],
        .main_class="org/javaswitch/test/Java25Smoke",.arguments=args,.argument_count=1,.heap_mib=128,.on_vm_ready=ready};
    const char *options[]={"-Djs.option.forwarded=yes"};
    JsLaunchResult r;int code=js_runtime_launch_with_options(&q,options,1,&r);printf("JNI result=%d stage=%s\n",code,r.stage);return code?1:0;
}
