/* SPDX-License-Identifier: GPL-2.0-only */
#include <jni.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern jboolean Java_sun_net_spi_DefaultProxySelector_init(JNIEnv*, jclass);
extern jobjectArray Java_sun_net_spi_DefaultProxySelector_getSystemProxies(
    JNIEnv*, jobject, jstring, jstring);
extern jobjectArray Java_java_net_NetworkInterface_getAll(JNIEnv*, jclass);
extern jboolean Java_java_net_NetworkInterface_isUp0(
    JNIEnv*, jclass, jstring, jint);
extern jint Java_java_net_NetworkInterface_getMTU0(
    JNIEnv*, jclass, jstring, jint);

static unsigned g_exception_count;
static char g_exception_class[96];

void JNU_ThrowByName(JNIEnv* env, const char* class_name, const char* message) {
  (void)env;
  (void)message;
  ++g_exception_count;
  strncpy(g_exception_class, class_name, sizeof(g_exception_class) - 1);
  g_exception_class[sizeof(g_exception_class) - 1] = '\0';
}

static void expect_exception(const char* expected) {
  assert(strcmp(g_exception_class, expected) == 0);
}

int main(void) {
  assert(Java_sun_net_spi_DefaultProxySelector_init(NULL, NULL) == JNI_FALSE);
  assert(Java_sun_net_spi_DefaultProxySelector_getSystemProxies(
      NULL, NULL, NULL, NULL) == NULL);
  expect_exception("java/lang/UnsupportedOperationException");
  assert(g_exception_count == 1);

  const unsigned before_network_queries = g_exception_count;
  assert(Java_java_net_NetworkInterface_getAll(NULL, NULL) == NULL);
  expect_exception("java/net/SocketException");
  assert(Java_java_net_NetworkInterface_isUp0(NULL, NULL, NULL, 0) == JNI_FALSE);
  expect_exception("java/net/SocketException");
  assert(Java_java_net_NetworkInterface_getMTU0(NULL, NULL, NULL, 0) == -1);
  expect_exception("java/net/SocketException");
  assert(g_exception_count == before_network_queries + 3);
  puts("Horizon unsupported proxy/interface queries report explicit errors: PASS");
  return 0;
}
