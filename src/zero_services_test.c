/* Standalone UI: keep libnx's C typedefs out of HotSpot C++ translation units. */
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
extern int js_zero_services_probe(void);
extern void js_zero_thread_probe(void);
void js_zero_probe_error(const char *format, va_list args) {
    char text[1024]; vsnprintf(text, sizeof(text), format, args);
    sc_log("%s\n", text);
}
int main(void) {
    sc_init("zero-services-test");
    sc_log("OpenJDK25 Horizon OS-service components only\n"
           "NO Java VM, NO classfile execution; hardware NOT TESTED.\n"
           "A: time/synchronization probe | X: native thread/permit probe | +: exit\n");
    u64 keys;
    while (sc_frame(&keys)) {
        if (keys & HidNpadButton_A) js_zero_services_probe();
        if (keys & HidNpadButton_X) js_zero_thread_probe();
    }
    sc_exit();
    return 0;
}
