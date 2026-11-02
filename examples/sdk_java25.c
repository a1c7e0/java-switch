#include <javaswitch/runtime.h>
#include <stdio.h>

int main(void) {
    JsRuntimeInfo info;
    int code = js_runtime_query("openjdk25", &info);
    if (code != JS_OK) return 1;
    printf("backend=%s ABI=%u Java=%u available=%d standard_jni=%d\n",
           info.name, info.abi_version, info.java_major, info.available, info.standard_jni);
    JsLaunchRequest request = {
        .abi_version = JS_ABI_VERSION,
        .required_java_major = 25,
        .backend = "openjdk25",
        .classpath = "sdmc:/switch/mc-runtime/tests/java25-hello.jar",
        .main_class = "HelloHorizon25",
        .heap_mib = 256
    };
    JsLaunchResult result;
    code = js_runtime_launch(&request, &result);
    printf("launch=%d stage=%s\n", code, result.stage);
    /* This diagnostic expects an honest refusal until a real VM is linked. */
    return code == JS_UNAVAILABLE ? 0 : 1;
}
