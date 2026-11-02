# Static native resolver extension

`include/javaswitch/native_resolver.h` adds a separately versioned optional C
interface. `JS_NATIVE_RESOLVER_ABI_VERSION` is 1; `runtime.h` and its ABI v1
structures are unchanged. The extension does not provide a Horizon JVM, dynamic
loader, JNI thread attachment, or FFM closure ownership.

Install one `JsNativeResolver` before attempting VM creation. Its two callbacks
identify actual statically linked modules and return actual function addresses:

```c
JsNativeResolver resolver = {
    .abi_version = JS_NATIVE_RESOLVER_ABI_VERSION,
    .struct_size = sizeof(JsNativeResolver),
    .module_available = module_available,
    .resolve = resolve_symbol,
    .context = application_context
};
int status = js_native_resolver_install(&resolver);
```

Named modules use exact names agreed with the provider. A null module denotes
the process-wide symbol scope; an empty module name is invalid. A nonzero
`module_available` result must identify a real resident implementation. The
resolver returns zero for missing functions. `js_native_module_available` and
`js_native_symbol_lookup` return JS_UNAVAILABLE for missing providers, modules
or functions, resetting their outputs. Invalid arguments return JS_BAD_ARGUMENT.

The SDK copies the descriptor, but does not own its context or returned code.
Keep callbacks, context and all returned functions alive for the complete VM
lifetime. Use exact C/AAPCS64 prototypes at call sites. For an FFM executable
closure, return the executable address, retain its arena/allocation and honor
the existing W^X transitions; a writable alias is not a function address.

Installation is immutable. Duplicate installation and installation after
`js_native_resolver_seal` return JS_INCOMPATIBLE. The JNI adapter seals immediately
before calling a genuine linked JNI_CreateJavaVM; an unavailable VM does not
seal it. Sealing also applies if that VM creation attempt fails. Callbacks run
outside the publication lock and may perform lookups; they must not attempt to
replace the resolver. Snapshot publication uses real C11 atomic synchronization.

The experimental Horizon loader consumes this extension for named/static
lookup. Resident code is not unloaded: only private lookup tokens can be freed.
No desktop shared library is loaded on Horizon. A provider must register core
JDK libraries as well as application natives when a genuine VM/image exists.
An empty resolver cannot make an absent library available.

`tests/native_resolver_host.c` resolves and actually invokes an exact-prototype
C function, checks null/global and named scopes, missing values, malformed
descriptors and lifetime gates. The SDK host JNI/FFM reference tests remain
separate. Native AArch64 compilation is not a physical Switch execution result.
