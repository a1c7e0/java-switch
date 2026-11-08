#pragma once

/* Private startup diagnostics shared by the Horizon test harness and provider. */
enum {
    JS_DIAG_IDLE = 0,
    JS_DIAG_MAIN_BEFORE_WORKER = 1,
    JS_DIAG_WORKER_ENTRY = 2,
    JS_DIAG_SDK_LAUNCH_ENTRY = 3,
    JS_DIAG_JNI_CREATE_ENTRY = 4,
    JS_DIAG_JNI_CREATE_RETURN = 5,
    JS_DIAG_VM_READY = 6
};

#ifdef __cplusplus
extern "C" {
#endif
void sc_diag_stage(unsigned stage);
#ifdef __cplusplus
}
#endif
