/* SPDX-License-Identifier: GPL-2.0-only */
#include <jni.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern jstring Java_sun_net_dns_ResolverConfigurationImpl_fallbackDomain0(
    JNIEnv*, jclass);

static const char* g_hostname;
static char g_result[128];
static const jstring g_sentinel = (jstring)(uintptr_t)1;

int __wrap_gethostname(char* name, size_t length) {
  size_t size = strlen(g_hostname);
  if (length == 0) return -1;
  if (size >= length) size = length - 1;
  memcpy(name, g_hostname, size);
  name[size] = '\0';
  return 0;
}

static jstring fake_new_string_utf(JNIEnv* env, const char* text) {
  (void)env;
  strncpy(g_result, text, sizeof(g_result) - 1);
  g_result[sizeof(g_result) - 1] = '\0';
  return (jstring)g_sentinel;
}

int main(void) {
  struct JNINativeInterface_ table = {0};
  table.NewStringUTF = fake_new_string_utf;
  JNIEnv jni = &table;
  JNIEnv* env = &jni;

  g_hostname = "switch.local.example";
  assert(Java_sun_net_dns_ResolverConfigurationImpl_fallbackDomain0(env, NULL) ==
         g_sentinel);
  assert(strcmp(g_result, "local.example") == 0);

  g_hostname = "switch";
  assert(Java_sun_net_dns_ResolverConfigurationImpl_fallbackDomain0(env, NULL) ==
         NULL);
  puts("Horizon resolver fallback uses only the reported hostname: PASS");
  return 0;
}
