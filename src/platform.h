#pragma once
#include <switch.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SC_ROOT "sdmc:/switch/mc-runtime"
#define SC_LOGS SC_ROOT "/logs"
void sc_init(const char *name);
void sc_exit(void);
void sc_console(bool enabled);
void sc_log(const char *format, ...) __attribute__((format(printf,1,2)));
bool sc_frame(u64 *down);
bool sc_chain(const char *name);
void sc_memory(void);
void sc_fs_test(void);
void sc_thread_test(void);
void sc_mkdirs(const char *path);
#ifdef __cplusplus
}
#endif
