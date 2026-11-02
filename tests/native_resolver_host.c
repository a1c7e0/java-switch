#include "javaswitch/native_resolver.h"

#include <stdio.h>
#include <string.h>

static int native_add(int left, int right) {
    return left + right;
}

static uintptr_t function_address(int (*function)(int, int)) {
    return (uintptr_t)function;
}

static int test_module_available(const char *module, void *context) {
    (void)context;
    return module == NULL || strcmp(module, "test-native") == 0;
}

static uintptr_t test_resolve(const char *module, const char *symbol, void *context) {
    (void)context;
    if (test_module_available(module, NULL) && symbol != NULL &&
        strcmp(symbol, "native_add") == 0) {
        return function_address(native_add);
    }
    return 0;
}

static int check_call(uintptr_t address) {
    int (*function)(int, int) = (int (*)(int, int))address;
    return function(19, 23) == 42;
}

int main(void) {
    JsNativeResolver resolver = {
        JS_NATIVE_RESOLVER_ABI_VERSION,
        sizeof(JsNativeResolver),
        test_module_available,
        test_resolve,
        NULL
    };
    JsNativeResolver bad;
    int available = 7;
    uintptr_t address = 1;

    if (js_native_resolver_install(NULL) != JS_BAD_ARGUMENT) return 1;
    bad = resolver;
    bad.abi_version++;
    if (js_native_resolver_install(&bad) != JS_BAD_ARGUMENT) return 2;
    bad = resolver;
    bad.struct_size = sizeof(JsNativeResolver) - 1;
    if (js_native_resolver_install(&bad) != JS_BAD_ARGUMENT) return 3;
    bad = resolver;
    bad.resolve = NULL;
    if (js_native_resolver_install(&bad) != JS_BAD_ARGUMENT) return 4;
    if (js_native_resolver_install(&resolver) != JS_OK) return 5;

    if (js_native_module_available("test-native", &available) != JS_OK || !available) return 6;
    available = 9;
    if (js_native_module_available("missing", &available) != JS_UNAVAILABLE || available != 0) return 7;
    if (js_native_module_available(NULL, &available) != JS_OK || !available) return 8;
    available = 9;
    if (js_native_module_available("", &available) != JS_BAD_ARGUMENT || available != 0) return 9;

    if (js_native_symbol_lookup("test-native", "native_add", &address) != JS_OK ||
        address == 0 || !check_call(address)) return 10;
    address = 5;
    if (js_native_symbol_lookup("missing", "native_add", &address) != JS_UNAVAILABLE ||
        address != 0) return 11;
    address = 5;
    if (js_native_symbol_lookup("test-native", "missing", &address) != JS_UNAVAILABLE ||
        address != 0) return 12;
    if (js_native_symbol_lookup(NULL, "native_add", &address) != JS_OK ||
        address == 0 || !check_call(address)) return 13;
    address = 5;
    if (js_native_symbol_lookup("test-native", "", &address) != JS_BAD_ARGUMENT || address != 0) return 14;
    if (js_native_resolver_install(&resolver) != JS_INCOMPATIBLE) return 15;
    if (js_native_resolver_seal() != JS_OK) return 16;
    if (js_native_resolver_install(&resolver) != JS_INCOMPATIBLE) return 17;
    if (js_native_symbol_lookup("test-native", "native_add", &address) != JS_OK ||
        !check_call(address)) return 18;

    puts("native resolver host test passed");
    return 0;
}
