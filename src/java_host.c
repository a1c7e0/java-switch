/* Runs the same interpreter adapter and registered native bridge on Linux.
 * This verifies bytecode behavior, not Horizon service behavior. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
extern int sc_java_run(const char *,int);
void sc_log(const char *format,...) {va_list a;va_start(a,format);vprintf(format,a);va_end(a);fflush(stdout);}
int mbedtls_hardware_poll(void *ctx,unsigned char *out,size_t n,size_t *written) {
    (void)ctx;ssize_t result=getrandom(out,n,0);
    if(result<0)return -1;
    *written=(size_t)result;return 0;
}
int main(int argc,char **argv){if(argc!=2)return 2;return sc_java_run(argv[1],0);}
