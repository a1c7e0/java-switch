/* SPDX-License-Identifier: MIT */
#include "javaswitch/native_resolver.h"

#include <stdatomic.h>
#include <string.h>

enum {
    RESOLVER_INSTALLED = 1u,
    RESOLVER_SEALED = 2u
};

static atomic_flag resolver_lock = ATOMIC_FLAG_INIT;
static _Atomic unsigned resolver_state = 0;
static JsNativeResolver installed_resolver;

static void lock_resolver(void) {
    while (atomic_flag_test_and_set_explicit(&resolver_lock, memory_order_acquire)) {
        /* Installation and snapshots are short; callbacks run after unlocking. */
    }
}

static void unlock_resolver(void) {
    atomic_flag_clear_explicit(&resolver_lock, memory_order_release);
}

static int resolver_snapshot(JsNativeResolver *snapshot) {
    int installed;
    lock_resolver();
    installed = (atomic_load_explicit(&resolver_state, memory_order_relaxed) &
                 RESOLVER_INSTALLED) != 0;
    if (installed) *snapshot = installed_resolver;
    unlock_resolver();
    return installed;
}

static int valid_name(const char *name) {
    return name == NULL || name[0] != '\0';
}

int js_native_resolver_install(const JsNativeResolver *resolver) {
    unsigned state;

    if (resolver == NULL ||
        resolver->abi_version != JS_NATIVE_RESOLVER_ABI_VERSION ||
        resolver->struct_size < sizeof(JsNativeResolver) ||
        resolver->module_available == NULL || resolver->resolve == NULL) {
        return JS_BAD_ARGUMENT;
    }

    lock_resolver();
    state = atomic_load_explicit(&resolver_state, memory_order_relaxed);
    if (state != 0) {
        unlock_resolver();
        return JS_INCOMPATIBLE;
    }
    memcpy(&installed_resolver, resolver, sizeof(installed_resolver));
    atomic_store_explicit(&resolver_state, RESOLVER_INSTALLED, memory_order_release);
    unlock_resolver();
    return JS_OK;
}

int js_native_resolver_seal(void) {
    lock_resolver();
    atomic_fetch_or_explicit(&resolver_state, RESOLVER_SEALED, memory_order_release);
    unlock_resolver();
    return JS_OK;
}

int js_native_module_available(const char *module, int *available) {
    JsNativeResolver resolver;
    int found;

    if (available == NULL) return JS_BAD_ARGUMENT;
    *available = 0;
    if (!valid_name(module)) return JS_BAD_ARGUMENT;
    if (!resolver_snapshot(&resolver)) return JS_UNAVAILABLE;

    found = resolver.module_available(module, resolver.context);
    if (!found) return JS_UNAVAILABLE;
    *available = 1;
    return JS_OK;
}

int js_native_symbol_lookup(const char *module, const char *symbol,
                            uintptr_t *address) {
    JsNativeResolver resolver;
    uintptr_t resolved;

    if (address == NULL) return JS_BAD_ARGUMENT;
    *address = 0;
    if (!valid_name(module) || symbol == NULL || symbol[0] == '\0') return JS_BAD_ARGUMENT;
    if (!resolver_snapshot(&resolver)) return JS_UNAVAILABLE;
    if (!resolver.module_available(module, resolver.context)) return JS_UNAVAILABLE;

    resolved = resolver.resolve(module, symbol, resolver.context);
    if (resolved == 0) return JS_UNAVAILABLE;
    *address = resolved;
    return JS_OK;
}
