/* Experimental genuine VM launcher. Built only by the explicit link-vm action.
 * Missing runtime/native services are link failures; never replaced by stubs. */
#include "platform.h"
#include <javaswitch/runtime.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#ifdef JS_OPENJDK25_STATIC_PROVIDER
#include <javaswitch/native_resolver.h>
#include <jni.h>
extern const JsNativeResolver js_openjdk25_native_resolver;
#endif

/* libnx otherwise commits almost the entire process budget to newlib. Keep
 * physical budget available for HotSpot Alias mappings. Application mode only;
 * a loader-provided heap override takes precedence and must be diagnosed. */
size_t __nx_heap_size = 256u * 1024u * 1024u;
u64 __nx_exception_stack_size = 64u * 1024u;
u8 __nx_exception_stack[64u * 1024u] __attribute__((aligned(16)));

/* libnx invokes this on its dedicated exception stack and performs the
 * stock svcBreak path after it returns. This is diagnostic logging only; it
 * is not crash-safe and does not recover a failed VM. */
void __libnx_exception_handler(ThreadExceptionDump *dump) {
    if (dump == NULL) {
        sc_log("libnx exception dump is NULL\n");
        return;
    }
    sc_log("libnx fatal exception: descriptor=0x%08x PC=0x%016llx "
           "ESR=0x%08x FAR=0x%016llx\n",
           dump->error_desc,
           (unsigned long long)dump->pc.x,
           dump->esr,
           (unsigned long long)dump->far.x);
}

#ifdef JS_OPENJDK25_STATIC_PROVIDER
#define JAVA25_WORKER_STACK_SIZE (4u * 1024u * 1024u)
#define JAVA25_VM_HEAP_MIB 128u

typedef struct {
    JsLaunchRequest request;
    const char *options[1];
    size_t option_count;
    JsLaunchResult result;
    int status;
    atomic_bool complete;
} Java25LaunchWork;

typedef enum {
    JAVA25_WORKER_UNUSED,
    JAVA25_WORKER_CLAIMED,
    JAVA25_WORKER_CREATED,
    JAVA25_WORKER_STARTED,
    JAVA25_WORKER_JOINED,
    JAVA25_WORKER_CLOSED,
    JAVA25_WORKER_RETAINED
} Java25WorkerOwnership;

/* libnx and the worker callback may retain these addresses until process exit.
 * The VM is one-shot, so never reset or reuse them after claiming the worker. */
static Thread java25_worker;
static Java25LaunchWork java25_work;
static atomic_bool java25_worker_claimed;
static Java25WorkerOwnership java25_worker_ownership = JAVA25_WORKER_UNUSED;

static const char *const java25_smoke_arguments[] = {
    "sdmc:/switch/mc-runtime/tmp"
};
static const char *const java25_smoke_options[] = {
    "-Djs.option.forwarded=yes"
};

typedef enum {
    JAVA25_MODE_HELLO,
    JAVA25_MODE_SMOKE
} Java25Mode;

static jint java25_smoke_native_add(JNIEnv *env, jclass klass,
                                    jint left, jint right) {
    (void)env;
    (void)klass;
    return left + right;
}

static int java25_smoke_register_natives(void *vm, void *opaque_env,
                                         void *context) {
    (void)vm;
    (void)context;
    JNIEnv *env = (JNIEnv *)opaque_env;
    jclass klass = (*env)->FindClass(env, "org/javaswitch/test/Java25Smoke");
    if (klass == NULL || (*env)->ExceptionCheck(env)) {
        sc_log("Java25 smoke JNI registration failed at FindClass(" 
               "org/javaswitch/test/Java25Smoke); pending_exception=%d\n",
               (*env)->ExceptionCheck(env));
        return -1;
    }

    JNINativeMethod methods[] = {
        { "nativeAdd", "(II)I", (void *)java25_smoke_native_add }
    };
    jint rc = (*env)->RegisterNatives(env, klass, methods, 1);
    (*env)->DeleteLocalRef(env, klass);
    if (rc != JNI_OK || (*env)->ExceptionCheck(env)) {
        sc_log("Java25 smoke JNI registration failed at RegisterNatives(" 
               "nativeAdd(II)I); result=%d pending_exception=%d\n",
               rc, (*env)->ExceptionCheck(env));
        return -1;
    }
    sc_log("Java25 smoke JNI RegisterNatives completed: nativeAdd(II)I\n");
    return 0;
}

static bool select_java25_mode(Java25Mode *mode) {
    sc_log("A: classfile-69 Hello | X: full Java25Smoke suite | +: exit\n"
           "One VM launch per process.\n");
    u64 keys;
    while (sc_frame(&keys)) {
        if (keys & HidNpadButton_X) {
            *mode = JAVA25_MODE_SMOKE;
            return true;
        }
        if (keys & HidNpadButton_A) {
            *mode = JAVA25_MODE_HELLO;
            return true;
        }
    }
    return false;
}

static void java25_launch_worker(void *opaque) {
    Java25LaunchWork *work = (Java25LaunchWork *)opaque;
    work->status = js_runtime_launch_with_options(
        &work->request, work->options, work->option_count, &work->result);
    atomic_store_explicit(&work->complete, true, memory_order_release);
}

static bool redirect_vm_output(void) {
    const char *stdout_path = SC_LOGS "/java25-vm-stdout.log";
    const char *stderr_path = SC_LOGS "/java25-vm-stderr.log";

    fflush(stdout);
    fflush(stderr);
    /* freopen can close a stream even when reopening fails. Keep status logs
     * on the existing SD log FILE without vprintf through a closed stdout. */
    sc_log_console(false);
    if (freopen(stdout_path, "a", stdout) == NULL) {
        int error = errno;
        /* sc_log calls vprintf, which can touch the failed stdout stream. */
        fprintf(stderr, "VM stdout redirect FAILED: %s errno=%d\n",
                stdout_path, error);
        fflush(stderr);
        return false;
    }
    if (freopen(stderr_path, "a", stderr) == NULL) {
        int error = errno;
        sc_log("VM stderr redirect FAILED: %s errno=%d\n", stderr_path, error);
        return false;
    }
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0 ||
        setvbuf(stderr, NULL, _IOLBF, 0) != 0) {
        sc_log("VM output line buffering could not be configured\n");
        return false;
    }
    return true;
}

static int run_java25_worker(const JsLaunchRequest *request,
                             const char *const *options, size_t option_count,
                             JsLaunchResult *result) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &java25_worker_claimed, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        *result = (JsLaunchResult){ JS_UNAVAILABLE, "worker-already-claimed" };
        sc_log("Java25 worker is one-shot; existing worker ownership state=%d\n",
               java25_worker_ownership);
        return JS_UNAVAILABLE;
    }
    java25_worker_ownership = JAVA25_WORKER_CLAIMED;
    memset(&java25_work, 0, sizeof(java25_work));
    memset(&java25_worker, 0, sizeof(java25_worker));
    java25_work.request = *request;
    if (option_count > sizeof(java25_work.options) / sizeof(java25_work.options[0])) {
        *result = (JsLaunchResult){ JS_BAD_ARGUMENT, "too-many-vm-options" };
        sc_log("Java25 worker received %zu extra options; capacity=%zu\n",
               option_count,
               sizeof(java25_work.options) / sizeof(java25_work.options[0]));
        return JS_BAD_ARGUMENT;
    }
    java25_work.option_count = option_count;
    for (size_t i = 0; i < option_count; ++i) {
        java25_work.options[i] = options[i];
    }
    java25_work.result = (JsLaunchResult){ JS_UNAVAILABLE, "worker-not-started" };
    java25_work.status = JS_UNAVAILABLE;
    atomic_init(&java25_work.complete, false);

    Result rc = threadCreate(&java25_worker, java25_launch_worker, &java25_work, NULL,
                             JAVA25_WORKER_STACK_SIZE, 0x2C, -2);
    if (R_FAILED(rc)) {
        *result = (JsLaunchResult){ JS_UNAVAILABLE, "threadCreate" };
        sc_log("Java25 worker threadCreate failed: Result=0x%x\n", rc);
        return JS_UNAVAILABLE;
    }
    java25_worker_ownership = JAVA25_WORKER_CREATED;

    rc = threadStart(&java25_worker);
    if (R_FAILED(rc)) {
        sc_log("Java25 worker threadStart failed: Result=0x%x\n", rc);
        Result close_rc = threadClose(&java25_worker);
        if (R_FAILED(close_rc)) {
            java25_worker_ownership = JAVA25_WORKER_RETAINED;
            sc_log("Unstarted worker threadClose failed: Result=0x%x; "
                   "libnx still owns its thread resources until process exit\n",
                   close_rc);
            *result = (JsLaunchResult){ JS_UNAVAILABLE, "worker-thread-close" };
        } else {
            java25_worker_ownership = JAVA25_WORKER_CLOSED;
            *result = (JsLaunchResult){ JS_UNAVAILABLE, "threadStart" };
        }
        return JS_UNAVAILABLE;
    }
    java25_worker_ownership = JAVA25_WORKER_STARTED;

    consoleUpdate(NULL);

    do {
        rc = threadWaitForExit(&java25_worker);
        if (R_FAILED(rc)) {
            /* Do not close a handle or release worker state while it may run. */
            sc_log("Java25 worker wait failed: Result=0x%x; retaining ownership and retrying.\n",
                   rc);
            consoleUpdate(NULL);
            svcSleepThread(1000000000ull);
        }
    } while (R_FAILED(rc));

    java25_worker_ownership = JAVA25_WORKER_JOINED;
    bool complete = atomic_load_explicit(&java25_work.complete, memory_order_acquire);
    int status;
    if (complete) {
        *result = java25_work.result;
        status = java25_work.status;
    } else {
        *result = (JsLaunchResult){ JS_UNAVAILABLE, "worker-exited-without-result" };
        status = JS_UNAVAILABLE;
    }
    Result close_rc = threadClose(&java25_worker);
    if (R_FAILED(close_rc)) {
        java25_worker_ownership = JAVA25_WORKER_RETAINED;
        sc_log("Completed Java25 worker threadClose failed: Result=0x%x; "
               "thread resources remain owned by libnx; launch status=%d stage=%s\n",
               close_rc, status, result->stage ? result->stage : "unknown");
        *result = (JsLaunchResult){ JS_UNAVAILABLE, "worker-thread-close" };
        return JS_UNAVAILABLE;
    }
    java25_worker_ownership = JAVA25_WORKER_CLOSED;
    return status;
}
#else
#define JAVA25_VM_HEAP_MIB 128u
#endif

int main(void) {
    sc_init("java25-test");
    sc_log("Genuine OpenJDK25 Zero startup diagnostic; device execution NOT TESTED\n");
    sc_memory();
    bool heap_override = envHasHeapOverride();
    u64 heap_override_size = heap_override ? envGetHeapOverrideSize() : 0;
    sc_log("Newlib configured heap=%zu bytes; loader heap override=%d size=%llu bytes\n",
           __nx_heap_size, heap_override,
           (unsigned long long)heap_override_size);
    sc_log("JVM request is -Xmx%uM; process total/used budget is reported above and "
           "the loader override takes precedence over the configured newlib heap.\n",
           JAVA25_VM_HEAP_MIB);
#ifdef JS_OPENJDK25_STATIC_PROVIDER
    Java25Mode mode;
    if (!select_java25_mode(&mode)) {
        sc_log("Java25 startup canceled before VM creation\n");
        sc_exit();
        return 0;
    }
    sc_log("Selected Java25 mode=%s\n",
           mode == JAVA25_MODE_SMOKE ? "Java25Smoke" : "classfile-69-Hello");
#endif
    JsRuntimeInfo info = {0};
    int status = js_runtime_query("openjdk25", &info);
    sc_log("runtime query status=%d available=%d Java=%u JNI=%d\n",
           status, info.available, info.java_major, info.standard_jni);
#ifdef JS_OPENJDK25_STATIC_PROVIDER
    JsLaunchRequest request = {
        .abi_version = JS_ABI_VERSION, .required_java_major = 25,
        .backend = "openjdk25", .heap_mib = JAVA25_VM_HEAP_MIB,
        .classpath = "sdmc:/switch/mc-runtime/tests/java25-hello.jar",
        .main_class = "HelloHorizon25"
    };
    const char *const *extra_options = NULL;
    size_t extra_option_count = 0;
    if (mode == JAVA25_MODE_SMOKE) {
        sc_mkdirs(SC_ROOT "/tmp");
        request.classpath = "sdmc:/switch/mc-runtime/tests/java25-tests.jar";
        request.main_class = "org/javaswitch/test/Java25Smoke";
        request.arguments = java25_smoke_arguments;
        request.argument_count = sizeof(java25_smoke_arguments) /
                                 sizeof(java25_smoke_arguments[0]);
        request.on_vm_ready = java25_smoke_register_natives;
        extra_options = java25_smoke_options;
        extra_option_count = sizeof(java25_smoke_options) /
                             sizeof(java25_smoke_options[0]);
    }
#endif
    JsLaunchResult result = {0};
#ifdef JS_OPENJDK25_STATIC_PROVIDER
    int resolver_status = js_native_resolver_install(&js_openjdk25_native_resolver);
    if (resolver_status != JS_OK) {
        status = resolver_status;
        result = (JsLaunchResult){ status, "native-resolver-install" };
        sc_log("Static native resolver install failed: status=%d; VM startup not attempted\n",
               resolver_status);
    } else {
        resolver_status = js_native_resolver_seal();
        if (resolver_status != JS_OK) {
            status = resolver_status;
            result = (JsLaunchResult){ status, "native-resolver-seal" };
            sc_log("Static native resolver seal failed: status=%d; VM startup not attempted\n",
                   resolver_status);
        } else if (status != JS_OK || !info.available || info.java_major != 25 ||
                   !info.standard_jni) {
            status = JS_UNAVAILABLE;
            result = (JsLaunchResult){ status, "openjdk25-runtime-not-linked" };
            sc_log("OpenJDK25 JNI runtime is unavailable; VM startup not attempted\n");
        } else {
            sc_log("OpenJDK25 will run on a 4 MiB worker; UI updates pause during the "
                   "worker join. Initial diagnostics are in "
                   "sdmc:/switch/mc-runtime/logs/java25-test.log; VM stdout/stderr will "
                   "append to sdmc:/switch/mc-runtime/logs/java25-vm-stdout.log and "
                   "sdmc:/switch/mc-runtime/logs/java25-vm-stderr.log. If startup "
                   "hangs, use Home and close the application from the system UI.\n");
            consoleUpdate(NULL);
            if (!redirect_vm_output()) {
                status = JS_UNAVAILABLE;
                result = (JsLaunchResult){ status, "vm-output-redirect" };
                sc_log("VM startup not attempted because SD stdout/stderr capture failed\n");
            } else {
                status = run_java25_worker(&request, extra_options,
                                           extra_option_count, &result);
            }
        }
    }
#else
    status = JS_UNAVAILABLE;
    result = (JsLaunchResult){ status, "static-native-resolver-not-linked" };
    sc_log("Static OpenJDK25 native resolver provider is not linked; "
           "Java VM startup was NOT attempted.\n");
#endif
    sc_log("Java25 launch status=%d stage=%s\n", status, result.stage ? result.stage : "unknown");
    sc_log("JAR success requires the actual VM output files; no Minecraft test here.\n");
    u64 keys;
    while (sc_frame(&keys)) { }
    sc_log("Closing this application process; reopen application-mode hbmenu for another VM test.\n");
    fflush(NULL);
    sc_exit();
    /* DestroyJavaVM does not make process-lifetime native VM/reaper code safe
     * to unload into hbloader. Terminate the actual Horizon process. */
    svcExitProcess();
}
