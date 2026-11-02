#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JS_ABI_VERSION 1
enum { JS_OK=0, JS_UNAVAILABLE=-100, JS_INCOMPATIBLE=-101, JS_BAD_ARGUMENT=-102, JS_JAVA_EXCEPTION=-103 };
typedef struct {
    unsigned abi_version;
    unsigned java_major;
    int available;
    int standard_jni;
    const char *name;
} JsRuntimeInfo;
typedef int (*JsVmReady)(void *vm,void *env,void *context);
typedef struct {
    unsigned abi_version;
    unsigned required_java_major;
    const char *backend;
    const char *boot_classpath;
    const char *classpath;
    const char *main_class; /* slash-separated JVM binary name */
    const char *const *arguments;
    size_t argument_count;
    unsigned heap_mib;
    JsVmReady on_vm_ready;
    void *context;
} JsLaunchRequest;
typedef struct {int code;const char *stage;} JsLaunchResult;
int js_runtime_query(const char *backend,JsRuntimeInfo *info);
int js_runtime_launch(const JsLaunchRequest *request,JsLaunchResult *result);
int js_runtime_launch_with_options(const JsLaunchRequest *request,const char *const *options,size_t option_count,JsLaunchResult *result);
#ifdef __cplusplus
}
#endif
