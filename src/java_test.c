#include "platform.h"
#include <stdio.h>
#include <time.h>
extern int sc_java_run(const char *,int);
int timespec_get(struct timespec *value,int base) {
    if(base!=1)return 0;
    return clock_gettime(CLOCK_REALTIME,value)==0?base:0;
}
/* mbedTLS entropy uses the Horizon CSPRNG, never a fabricated /dev/urandom. */
int mbedtls_hardware_poll(void *ctx,unsigned char *output,size_t size,size_t *written) {
    (void)ctx;randomGet(output,size);*written=size;return 0;
}
int main(void) {
    sc_init("java-test");sc_mkdirs(SC_ROOT "/instances/java-smoke");sc_mkdirs(SC_ROOT "/tmp");
    sc_log("EXPERIMENTAL miniJVM interpreter diagnostic\nNot OpenJDK; no Minecraft/LWJGL/Fabric support.\nA: bundled suite | X: tests/user.jar class UserMain\nOne run per process | B: diagnostics | +: exit\n");
    bool ran=false;u64 keys;
    while(sc_frame(&keys)) {
        if((keys&(HidNpadButton_A|HidNpadButton_X))&&!ran){
            int user=(keys&HidNpadButton_X)!=0;
            FILE *f=fopen(SC_ROOT "/tests/user.jar","rb");
            if(user&&!f){sc_log("No user.jar installed\n");continue;}
            if(f)fclose(f);
            ran=true;int rc=sc_java_run(SC_ROOT,user);sc_log("Java suite return=%d (confirm every PASS line)\n",rc);
        }
        if((keys&HidNpadButton_B)&&sc_chain("switch-diagnostics"))break;
    }
    sc_exit();return 0;
}
