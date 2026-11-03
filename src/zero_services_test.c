/* Standalone UI: keep libnx's C typedefs out of HotSpot C++ translation units. */
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
extern int js_zero_services_probe(void);
extern void js_zero_thread_probe(void);
extern void js_zero_fault_probe(void);
extern void js_zero_reaper_probe(void);
extern void js_zero_memory_probe(void);
void __libnx_exception_handler(ThreadExceptionDump *context) {
    sc_log("UNHANDLED Horizon exception: desc=0x%x PC=0x%llx ESR=0x%x FAR=0x%llx\n",
        context->error_desc, (unsigned long long)context->pc.x, context->esr,
        (unsigned long long)context->far.x);
    /* Returning lets the preserved libnx path call svcBreak; no fake recovery. */
}
void js_zero_probe_error(const char *format, va_list args) {
    char text[1024]; vsnprintf(text, sizeof(text), format, args);
    sc_log("%s\n", text);
}
int main(void) {
    sc_init("zero-services-test");
    sc_log("OpenJDK25 Horizon OS-service components only\n"
           "NO Java VM, NO classfile execution; hardware NOT TESTED.\n"
           "A: time/synchronization probe | X: native thread/permit probe | +: exit\n");
    sc_log("Y: experimental fault-island probe; may abort if recovery is unsupported\n");
    sc_log("B: deferred native thread ownership / join / close probe\n");
    sc_log("L: private VM Alias memory / split release / SDK ownership probe\n");
    u64 keys;
    while (sc_frame(&keys)) {
        if (keys & HidNpadButton_A) js_zero_services_probe();
        if (keys & HidNpadButton_X) js_zero_thread_probe();
        if (keys & HidNpadButton_B) js_zero_reaper_probe();
        if (keys & HidNpadButton_L) js_zero_memory_probe();
        if (keys & HidNpadButton_Y) {
            MemoryInfo info; u32 page_info;
            Result result = svcQueryMemory(&info, &page_info, 0);
            sc_log("SafeFetch target address 0: query Result=0x%x permissions=0x%x\n", result, R_SUCCEEDED(result) ? info.perm : 0);
            if (R_SUCCEEDED(result) && !(info.perm & Perm_R)) js_zero_fault_probe();
            else sc_log("Fault probe skipped: target not confirmed unreadable\n");
        }
    }
    sc_log("Closing this application process; reopen application-mode hbmenu for another native test.\n");
    sc_exit();
    /* The reaper/retained probe workers must not outlive this NRO mapping. */
    svcExitProcess();
}
