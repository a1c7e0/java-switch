#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
int timespec_get(struct timespec *value,int base){if(base!=1)return 0;return clock_gettime(CLOCK_REALTIME,value)==0?base:0;}
int mbedtls_hardware_poll(void *ctx,unsigned char *out,size_t size,size_t *done){(void)ctx;randomGet(out,size);*done=size;return 0;}
#else
#include <sys/random.h>
int mbedtls_hardware_poll(void *ctx,unsigned char *out,size_t size,size_t *done){(void)ctx;ssize_t n=getrandom(out,size,0);if(n<0)return -1;*done=(size_t)n;return 0;}
#endif
extern void sc_log(const char *,...) __attribute__((weak));
int sc_java_vprintf(const char *format,va_list args){
    if(sc_log){char buffer[2048];int n=vsnprintf(buffer,sizeof(buffer),format,args);sc_log("%s",buffer);return n;}
    return vfprintf(stderr,format,args);
}
void sc_java_write_byte(unsigned char value){if(sc_log)sc_log("%c",value);else fputc(value,stdout);}
