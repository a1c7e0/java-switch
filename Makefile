DEVKITPRO ?= $(CURDIR)/.toolchain/opt/devkitpro
export DEVKITPRO
DK := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := $(ARCH) -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections -D__SWITCH__ -isystem $(DEVKITPRO)/libnx/include -Isrc -Iinclude
LDFLAGS := $(ARCH) -specs=$(DEVKITPRO)/libnx/switch.specs -L$(DEVKITPRO)/libnx/lib -Wl,--gc-sections
.PHONY: all host-test clean
all: dist/java-test.nro dist/ffi-test.nro build/minijvm_rt.jar build/diagnostic-tests.jar build/dependency.jar build/sdk/libjavaswitch.a build/java25-tests.jar build/java25-hello.jar
build dist:
	mkdir -p $@
build/platform.o: src/platform.c src/platform.h | build
	$(DK)gcc $(CFLAGS) -c $< -o $@
build/libminijvm.a: scripts/build-minijvm.py patches/minijvm-horizon.patch src/minijvm_mman.h src/minijvm_compat.h | build
	python3 scripts/build-minijvm.py
build/java_vm.o: src/java_vm.c build/libminijvm.a
	$(DK)gcc $(CFLAGS) -I.deps/minijvm/jvm -I.deps/minijvm/utils -c $< -o $@
build/java-test.elf: src/java_test.c build/java_vm.o build/platform.o build/libminijvm.a
	$(DK)gcc $(CFLAGS) $(LDFLAGS) $^ -lnx -lm -o $@ -Wl,-Map,$@.map
dist/java-test.nro: build/java-test.elf | dist
	$(DEVKITPRO)/tools/bin/nacptool --create java-test JavaSwitch 0.2.0 build/java-test.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/java-test.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/libffi-horizon.a: scripts/build-libffi.py | build
	python3 scripts/build-libffi.py
build/ffi-test.elf: src/ffi_test.c src/ffi_horizon.c src/vmem_horizon.c include/javaswitch/vmem.h build/platform.o build/libffi-horizon.a
	$(DK)gcc $(CFLAGS) -Ibuild/libffi/include $(LDFLAGS) $(filter-out %.h,$^) -lnx -lm -o $@
dist/ffi-test.nro: build/ffi-test.elf | dist
	$(DEVKITPRO)/tools/bin/nacptool --create ffi-test JavaSwitch 0.2.0 build/ffi-test.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/ffi-test.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/java-test-host: scripts/build-minijvm.py src/java_vm.c src/java_host.c build/libminijvm.a
	python3 scripts/build-minijvm.py --host
build/minijvm_rt.jar build/diagnostic-tests.jar build/dependency.jar &: scripts/build-java-tests.py tests/java/DiagnosticSmoke.java tests/java/JarDependency.java | build
	python3 scripts/build-java-tests.py
build/sdk/libjavaswitch.a build/jni25-host &: scripts/build-sdk.py src/runtime.c src/runtime_minijvm.c src/runtime_openjdk.c src/runtime_services.c src/ffi_horizon.c src/vmem_horizon.c include/javaswitch/vmem.h include/javaswitch/runtime.h tests/runtime_gate.c build/libminijvm.a build/java-test-host build/libffi-horizon.a
	python3 scripts/build-sdk.py
build/java25-tests.jar build/java25-hello.jar &: scripts/build-java25-tests.py tests/java25/Java25Smoke.java tests/java25/HelloHorizon25.java | build
	python3 scripts/build-java25-tests.py
host-test: build/java-test-host build/minijvm_rt.jar build/diagnostic-tests.jar build/dependency.jar build/jni25-host build/java25-tests.jar
	python3 -m unittest discover -s tests -v
clean:
	rm -rf build dist
