#include "platform.h"
#include "build-info.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <malloc.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/iosupport.h>

static FILE *log_file;
static PadState pad;
static const char *app_name;
static bool console_enabled=true;
static bool console_log_enabled=true;
static int diag_fd=-1;
static atomic_uint diag_stage;
static atomic_uint diag_heartbeat;
extern char _start[];
extern const devoptab_t *__nx_get_console_dotab(void);
static const char *const diag_stage_names[]={
    "idle", "main-before-worker", "worker-entry", "sdk-launch-entry",
    "jni-create-entry", "jni-create-return", "vm-ready"
};

static size_t append_text(char *buffer,size_t at,size_t capacity,const char *text) {
    while(*text && at<capacity) buffer[at++]=*text++;
    return at;
}
static size_t append_hex(char *buffer,size_t at,size_t capacity,uint64_t value,unsigned digits) {
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<digits && at<capacity;i++) {
        unsigned shift=(digits-1-i)*4;
        buffer[at++]=hex[(value>>shift)&15];
    }
    return at;
}
static void diag_write(const char *buffer,size_t size) {
    if(diag_fd<0)return;
    while(size) {
        ssize_t n=write(diag_fd,buffer,size);
        if(n<=0)return;
        buffer+=n;size-=(size_t)n;
    }
}
void sc_diag_init(void) {
    if(log_file) diag_fd=fileno(log_file);
    atomic_store_explicit(&diag_stage,0,memory_order_relaxed);
    atomic_store_explicit(&diag_heartbeat,0,memory_order_relaxed);
}
void sc_diag_stage(unsigned stage) {
    atomic_store_explicit(&diag_stage,stage,memory_order_release);
    atomic_fetch_add_explicit(&diag_heartbeat,1,memory_order_relaxed);
    if(stage<sizeof(diag_stage_names)/sizeof(diag_stage_names[0])) {
        char event[96];size_t n=append_text(event,0,sizeof(event)-1,"STAGE ");
        n=append_text(event,n,sizeof(event)-1,diag_stage_names[stage]);
        event[n++]='\n';diag_write(event,n);
    }
}
void sc_diag_report(void) {
    unsigned stage=atomic_load_explicit(&diag_stage,memory_order_acquire);
    unsigned heartbeat=atomic_load_explicit(&diag_heartbeat,memory_order_relaxed);
    const char *name=stage<sizeof(diag_stage_names)/sizeof(diag_stage_names[0])?diag_stage_names[stage]:"unknown";
    sc_log("Java25 diagnostic stage=%s heartbeat=%u\n",name,heartbeat);
}
void sc_diag_exception(const ThreadExceptionDump *dump) {
    if(!dump)return;
    char line[2048];size_t n=0,capacity=sizeof(line)-1;
    uintptr_t base=(uintptr_t)_start;
    n=append_text(line,n,capacity,"\nFATAL desc=");n=append_hex(line,n,capacity,dump->error_desc,8);
    n=append_text(line,n,capacity," stage=");
    unsigned stage=atomic_load_explicit(&diag_stage,memory_order_relaxed);
    n=append_text(line,n,capacity,stage<sizeof(diag_stage_names)/sizeof(diag_stage_names[0])?diag_stage_names[stage]:"unknown");
    n=append_text(line,n,capacity," heartbeat=");n=append_hex(line,n,capacity,atomic_load_explicit(&diag_heartbeat,memory_order_relaxed),8);
    n=append_text(line,n,capacity," base=");n=append_hex(line,n,capacity,base,16);
    n=append_text(line,n,capacity," PC=");n=append_hex(line,n,capacity,dump->pc.x,16);
    n=append_text(line,n,capacity," rel=");n=append_hex(line,n,capacity,dump->pc.x-base,16);
    n=append_text(line,n,capacity," ESR=");n=append_hex(line,n,capacity,dump->esr,8);
    n=append_text(line,n,capacity," FAR=");n=append_hex(line,n,capacity,dump->far.x,16);
    n=append_text(line,n,capacity," SP=");n=append_hex(line,n,capacity,dump->sp.x,16);
    n=append_text(line,n,capacity," FP=");n=append_hex(line,n,capacity,dump->fp.x,16);
    n=append_text(line,n,capacity," LR=");n=append_hex(line,n,capacity,dump->lr.x,16);
    for(unsigned i=0;i<29;i++) {
        n=append_text(line,n,capacity," x");n=append_hex(line,n,capacity,i,2);n=append_text(line,n,capacity,"=");
        n=append_hex(line,n,capacity,dump->cpu_gprs[i].x,16);
    }
    if(n<sizeof(line)) line[n++]='\n';
    diag_write(line,n);
}
static void screen_write(const char *text, size_t size) {
    const devoptab_t *console = __nx_get_console_dotab();
    if (console && console->write_r) console->write_r(_REENT, NULL, text, size);
}
static void screen_tail(const char *path) {
    char text[3073];
    if (!path || !*path) { screen_write("No capture file.\n", sizeof("No capture file.\n")-1); return; }
    FILE *file = fopen(path, "rb");
    if (!file) { screen_write("Capture file could not be read.\n", sizeof("Capture file could not be read.\n")-1); return; }
    long length = 0;
    if (fseek(file, 0, SEEK_END) == 0) length = ftell(file);
    long offset = length > 2500 ? length - 2500 : 0;
    if (fseek(file, offset, SEEK_SET) != 0) { fclose(file); return; }
    size_t size = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    if (!size) { screen_write("(No VM output was written.)\n", sizeof("(No VM output was written.)\n")-1); return; }
    size_t start = 0;
    if (offset) { while (start < size && text[start] != '\n') start++; if (start < size) start++; }
    /* Keep terminal control bytes out of the review pane. */
    for (size_t i = start; i < size; i++)
        if ((unsigned char)text[i] < 32 && text[i] != '\n' && text[i] != '\t') text[i] = '?';
    screen_write(text + start, size - start);
}
void sc_result_review(int status,const char *stage,const char *stdout_path,const char *stderr_path) {
    sc_console(true);
    char header[768], native_path[256];
    snprintf(native_path, sizeof(native_path), SC_LOGS "/%s.log", app_name);
    unsigned pane = 0; bool dirty = true, neutral = false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
        if (!held) neutral = true;
        if (neutral && (down & HidNpadButton_Plus)) break;
        if (neutral && (down & HidNpadButton_A)) { pane = 0; dirty = true; }
        if (neutral && (down & HidNpadButton_X)) { pane = 1; dirty = true; }
        if (neutral && (down & HidNpadButton_Y)) { pane = 2; dirty = true; }
        if (dirty) {
            screen_write("\x1b[2J\x1b[H", 7);
            int n = snprintf(header, sizeof(header),
                "%s: attempt finished\nstatus=%d stage=%s\n"
                "A: VM stdout | X: VM stderr | Y: native log | +: close process\n"
                "A new VM requires a fresh process. Output is also saved on SD.\n"
                "Showing: %s\n----------------------------------------\n",
                app_name, status, stage ? stage : "unknown",
                pane == 0 ? (stdout_path && *stdout_path ? stdout_path : "stdout unavailable") :
                pane == 1 ? (stderr_path && *stderr_path ? stderr_path : "stderr unavailable") : native_path);
            if (n > 0) screen_write(header, (size_t)n < sizeof(header) ? (size_t)n : sizeof(header)-1);
            screen_tail(pane == 0 ? stdout_path : pane == 1 ? stderr_path : native_path);
            dirty = false;
        }
        consoleUpdate(NULL);
    }
}
void sc_log_console(bool enabled) { console_log_enabled=enabled; }
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
    if(console_enabled && console_log_enabled) {va_start(args,format); vprintf(format,args); va_end(args);}
    if (log_file) {
        fprintf(log_file,"[tick=%llu] ",(unsigned long long)armGetSystemTick());
        va_start(args,format); vfprintf(log_file,format,args); va_end(args);
        fflush(log_file);
    }
}
void sc_init(const char *name) {
    app_name=name;
    console_enabled=true; console_log_enabled=true;
    consoleInit(NULL);
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    sc_mkdirs(SC_LOGS);
    char path[256]; snprintf(path,sizeof(path),SC_LOGS "/%s.log",name);
    log_file=fopen(path,"a");
    sc_diag_init();
    sc_log("\nSESSION app=%s runtime_source=%s | current run: UNVERIFIED\n",name,JS_BUILD_REVISION);
    u32 version=hosversionGet();
    sc_log("Applet type=%d (Application=%d); firmware=%u.%u.%u\n",
        appletGetAppletType(),AppletType_Application,(version>>16)&255,
        (version>>8)&255,version&255);
    sc_log("Log: %s (%s)\n",path,log_file?"open":"SD logging FAILED");
}
bool sc_frame(u64 *down) {
    if (!appletMainLoop()) return false;
    padUpdate(&pad); *down=padGetButtonsDown(&pad);
    if(console_enabled)consoleUpdate(NULL);
    return !(*down&HidNpadButton_Plus);
}
void sc_exit(void) {
    sc_log("Clean exit: %s\n",app_name);
    if (log_file) fclose(log_file);
    log_file=NULL; diag_fd=-1;
    if(console_enabled)consoleExit(NULL);
    console_enabled=false;
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
    u64 system_total=0,system_used=0;
    Result c=svcGetInfo(&system_total,InfoType_SystemResourceSizeTotal,CUR_PROCESS_HANDLE,0);
    Result d=svcGetInfo(&system_used,InfoType_SystemResourceSizeUsed,CUR_PROCESS_HANDLE,0);
    sc_log("Process system-resource memory total=%llu used=%llu bytes rc=0x%x/0x%x\n",
        (unsigned long long)system_total,(unsigned long long)system_used,c,d);
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
