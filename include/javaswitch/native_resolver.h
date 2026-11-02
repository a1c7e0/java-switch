/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "javaswitch/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JS_NATIVE_RESOLVER_ABI_VERSION 1u

/*
 * A static resolver describes functions already linked into the process.
 * A NULL module names the process-wide symbol scope; other module names are
 * exact names interpreted by the resolver. The descriptor and its context
 * remain valid for the VM lifetime. This API does not own returned functions,
 * FFM closures, or their arenas.
 */
typedef struct {
    unsigned abi_version;
    size_t struct_size;
    int (*module_available)(const char *module, void *context);
    uintptr_t (*resolve)(const char *module, const char *symbol, void *context);
    void *context;
} JsNativeResolver;

/* Install one immutable resolver before VM startup, then seal installation. */
int js_native_resolver_install(const JsNativeResolver *resolver);
int js_native_resolver_seal(void);

/* On success, available is 1. Missing modules return JS_UNAVAILABLE and 0. */
int js_native_module_available(const char *module, int *available);

/* On success, address receives the resolved static function address.
 * Missing modules or symbols return JS_UNAVAILABLE and leave address zero.
 */
int js_native_symbol_lookup(const char *module, const char *symbol,
                            uintptr_t *address);

#ifdef __cplusplus
}
#endif
