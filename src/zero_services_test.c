/* Standalone UI: keep libnx's C typedefs out of HotSpot C++ translation units. */
#include "platform.h"
extern int js_zero_services_probe(void);
int main(void) {
    sc_init("zero-services-test");
    sc_log("OpenJDK25 Horizon OS-service components only\n"
           "NO Java VM, NO classfile execution; hardware NOT TESTED.\n"
           "A: time/semaphore probe | +: exit\n");
    u64 keys;
    while (sc_frame(&keys))
        if (keys & HidNpadButton_A) js_zero_services_probe();
    sc_exit();
    return 0;
}
