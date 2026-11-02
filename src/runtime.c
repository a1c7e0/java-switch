#include "javaswitch/runtime.h"
#include <string.h>
extern int js_minijvm_launch(const JsLaunchRequest *,JsLaunchResult *);
extern int js_openjdk_available(void);
extern int js_openjdk_launch(const JsLaunchRequest *,JsLaunchResult *);
extern int js_openjdk_launch_with_options(const JsLaunchRequest *,const char *const *,size_t,JsLaunchResult *);
int js_runtime_query(const char *backend,JsRuntimeInfo *info) {
    if(!backend||!info)return JS_BAD_ARGUMENT;
    *info=(JsRuntimeInfo){.abi_version=JS_ABI_VERSION,.name=backend};
    if(!strcmp(backend,"minijvm8")){info->java_major=8;info->available=1;return JS_OK;}
    if(!strcmp(backend,"openjdk25")){info->java_major=25;info->available=js_openjdk_available();info->standard_jni=info->available;return JS_OK;}
    return JS_BAD_ARGUMENT;
}
int js_runtime_launch(const JsLaunchRequest *q,JsLaunchResult *result) {
    return js_runtime_launch_with_options(q,NULL,0,result);
}
int js_runtime_launch_with_options(const JsLaunchRequest *q,const char *const *options,size_t count,JsLaunchResult *result) {
    if(!result)return JS_BAD_ARGUMENT;
    *result=(JsLaunchResult){JS_BAD_ARGUMENT,"request-validation"};
    if(!q||q->abi_version!=JS_ABI_VERSION||!q->backend||!q->classpath||!q->main_class||q->argument_count>256||
       (q->argument_count&&!q->arguments)||q->heap_mib<32||q->heap_mib>1024)return result->code;
    if(count>128||(count&&!options))return result->code;
    for(size_t i=0;i<count;i++)if(!options[i]||options[i][0]!='-'||strlen(options[i])>16384)return result->code;
    JsRuntimeInfo info;
    if(js_runtime_query(q->backend,&info)!=JS_OK)return result->code;
    if(!info.available){result->code=JS_UNAVAILABLE;result->stage="runtime-not-linked";return result->code;}
    if(q->required_java_major>info.java_major){result->code=JS_INCOMPATIBLE;result->stage="java-version-mismatch";return result->code;}
    if(!strcmp(q->backend,"minijvm8"))return count?JS_BAD_ARGUMENT:js_minijvm_launch(q,result);
    return js_openjdk_launch_with_options(q,options,count,result);
}
