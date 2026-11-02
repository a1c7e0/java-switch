#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <malloc.h>

static FILE *log_file;
static PadState pad;
static const char *app_name;
static bool console_enabled=true;
void sc_console(bool enabled) {
    if(enabled==console_enabled)return;
    if(enabled)consoleInit(NULL); else consoleExit(NULL);
    console_enabled=enabled;
}
void sc_mkdirs(const char *path) {
    char tmp[768];
    if (strlen(path) >= sizeof(tmp)) return;
    strcpy(tmp, path);
    for (char *p=tmp+1; *p; ++p) if (*p=='/') {
        *p=0; mkdir(tmp,0777); *p='/';
    }
    mkdir(tmp,0777);
}
void sc_log(const char *format, ...) {
    va_list args;
    if(console_enabled) {va_start(args,format); vprintf(format,args); va_end(args);}
    if (log_file) {
        fprintf(log_file,"[tick=%llu] ",(unsigned long long)armGetSystemTick());
        va_start(args,format); vfprintf(log_file,format,args); va_end(args);
        fflush(log_file);
    }
}
void sc_init(const char *name) {
    app_name=name;
    consoleInit(NULL);
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    sc_mkdirs(SC_LOGS);
    char path[256]; snprintf(path,sizeof(path),SC_LOGS "/%s.log",name);
    log_file=fopen(path,"a");
    sc_log("\nSwitchcraft %s 0.1.0 | hardware status: NOT TESTED\n",name);
    u32 version=hosversionGet();
    sc_log("Applet type=%d (Application=%d); firmware=%u.%u.%u\n",
        appletGetAppletType(),AppletType_Application,(version>>16)&255,
        (version>>8)&255,version&255);
    sc_log("Log: %s (%s)\n",path,log_file?"open":"SD logging FAILED");
}
bool sc_frame(u64 *down) {
    if (!appletMainLoop()) return false;
    padUpdate(&pad); *down=padGetButtonsDown(&pad);
    consoleUpdate(NULL);
    return !(*down&HidNpadButton_Plus);
}
void sc_exit(void) {
    sc_log("Clean exit: %s\n",app_name);
    if (log_file) fclose(log_file);
    log_file=NULL; consoleExit(NULL);
}
bool sc_chain(const char *name) {
    char path[256]; snprintf(path,sizeof(path),"sdmc:/switch/%s/%s.nro",name,name);
    FILE *f=fopen(path,"rb");
    if (!f) {sc_log("Cannot open %s: errno=%d\n",path,errno); return false;}
    fclose(f);
    Result rc=envSetNextLoad(path,path);
    sc_log("envSetNextLoad(%s) = 0x%x\n",path,rc);
    return R_SUCCEEDED(rc);
}
void sc_memory(void) {
    u64 total=0,used=0;
    Result a=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    Result b=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    sc_log("Process memory: total=%llu used=%llu bytes rc=0x%x/0x%x\n",
        (unsigned long long)total,(unsigned long long)used,a,b);
    if (R_SUCCEEDED(a)&&R_SUCCEEDED(b)&&total>=used)
        sc_log("Process limit minus current use=%llu bytes (not JVM heap)\n",(unsigned long long)(total-used));
    struct mallinfo heap=mallinfo();
    sc_log("Newlib malloc arena=%llu allocated=%llu free-in-arena=%llu bytes\n",
        (unsigned long long)heap.arena,(unsigned long long)heap.uordblks,(unsigned long long)heap.fordblks);
    sc_log("Kernel-used memory includes reserved heap; arena free space is separate.\n");
    const size_t size=4*1024*1024;
    volatile unsigned char *p=malloc(size);
    if (!p) {sc_log("4 MiB allocation FAILED\n"); return;}
    for(size_t i=0;i<size;i++)p[i]=0xa5;
    bool ok=true; for(size_t i=0;i<size;i++) if(p[i]!=0xa5) {ok=false; break;}
    free((void *)p); sc_log("4 MiB allocation/write/read/free: %s\n",ok?"PASS":"FAIL");
}
void sc_fs_test(void) {
    const char *path=SC_ROOT "/diagnostic.tmp";
    FILE *f=fopen(path,"wx");
    if(!f) {sc_log("SD create-exclusive FAILED errno=%d; existing file preserved\n",errno); return;}
    const char text[]="Switchcraft SD roundtrip\n";
    bool ok=fwrite(text,1,sizeof(text),f)==sizeof(text);
    if(fclose(f)!=0)ok=false;
    char buffer[sizeof(text)]={0}; f=fopen(path,"rb");
    if(f) {ok &= fread(buffer,1,sizeof(buffer),f)==sizeof(buffer); fclose(f);} else ok=false;
    ok &= memcmp(buffer,text,sizeof(text))==0;
    int rc=remove(path);
    sc_log("SD write/read/compare: %s; remove=%d errno=%d\n",ok?"PASS":"FAIL",rc,errno);
}
static void worker(void *arg) {
    unsigned *value=arg;
    for(unsigned i=1;i<=1000;i++) *value+=i;
}
void sc_thread_test(void) {
    Thread thread; unsigned value=0;
    Result rc=threadCreate(&thread,worker,&value,NULL,64*1024,0x2c,-2);
    sc_log("threadCreate=0x%x\n",rc);
    if(R_FAILED(rc))return;
    rc=threadStart(&thread);
    if(R_SUCCEEDED(rc)) rc=threadWaitForExit(&thread);
    sc_log("thread start/join=0x%x sum=%u expected=500500: %s\n",rc,value,
        R_SUCCEEDED(rc)&&value==500500?"PASS":"FAIL");
    threadClose(&thread);
}
