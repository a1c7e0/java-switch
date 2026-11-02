/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JS_TEST_SWITCH_RESULT_H
#define JS_TEST_SWITCH_RESULT_H
#include <stdint.h>
typedef uint32_t Result;
enum { Module_Kernel = 1, Module_Libnx = 345, LibnxError_OutOfMemory = 9,
       KernelError_TimedOut = 117 };
#define MAKERESULT(module, description) (((module) & 0x1ff) | (((description) & 0x1fff) << 9))
#define KERNELRESULT(description) MAKERESULT(Module_Kernel, KernelError_##description)
#define R_MODULE(result) ((result) & 0x1ff)
#define R_VALUE(result) ((result) & 0x3fffff)
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)
#endif
