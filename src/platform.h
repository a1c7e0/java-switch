#pragma once
#include <switch.h>
#include <stdbool.h>
#include "runtime_diagnostics.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SC_ROOT "sdmc:/switch/mc-runtime"
#define SC_LOGS SC_ROOT "/logs"
void sc_init(const char *name);
void sc_exit(void);
void sc_console(bool enabled);
/* Keep the console active while redirecting native VM stdout/stderr. */
void sc_log_console(bool enabled);
void sc_log(const char *format, ...) __attribute__((format(printf,1,2)));
/* Crash diagnostics are initialized on the main thread before worker startup.
 * The exception path writes through the captured descriptor without stdio. */
void sc_diag_init(void);
void sc_diag_exception(const ThreadExceptionDump *dump);
void sc_diag_report(void);
void sc_result_review(int status, const char *stage,
                      const char *stdout_path, const char *stderr_path);
bool sc_frame(u64 *down);
bool sc_chain(const char *name);
void sc_memory(void);
void sc_fs_test(void);
void sc_thread_test(void);
void sc_mkdirs(const char *path);
#ifdef __cplusplus
}
#endif
