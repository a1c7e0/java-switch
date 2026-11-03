/* SPDX-License-Identifier: GPL-2.0-only */
#include <switch.h>

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

extern int NET_PlatformInit(void);

static unsigned g_initialize_calls;
static unsigned g_exit_calls;
static Result g_initialize_result;
static pthread_mutex_t g_mock_lock = PTHREAD_MUTEX_INITIALIZER;

void mutexLock(Mutex* mutex) { assert(pthread_mutex_lock(&mutex->native) == 0); }
void mutexUnlock(Mutex* mutex) { assert(pthread_mutex_unlock(&mutex->native) == 0); }

Result socketInitializeDefault(void) {
  assert(pthread_mutex_lock(&g_mock_lock) == 0);
  ++g_initialize_calls;
  assert(pthread_mutex_unlock(&g_mock_lock) == 0);
  return g_initialize_result;
}

void socketExit(void) { ++g_exit_calls; }

static void* initialize_worker(void* unused) {
  (void)unused;
  return (void*)(intptr_t)NET_PlatformInit();
}

int main(int argc, char** argv) {
  enum { THREAD_COUNT = 16 };
  pthread_t threads[THREAD_COUNT];
  int expected;
  if (argc == 2 && argv[1][0] == 'a') {
    g_initialize_result = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    expected = 0;
  } else if (argc == 2 && argv[1][0] == 'f') {
    g_initialize_result = 0x4567;
    expected = -1;
  } else {
    g_initialize_result = 0;
    expected = 0;
  }
  for (int i = 0; i < THREAD_COUNT; ++i)
    assert(pthread_create(&threads[i], NULL, initialize_worker, NULL) == 0);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    void* result = NULL;
    assert(pthread_join(threads[i], &result) == 0);
    assert((int)(intptr_t)result == expected);
  }
  assert(g_initialize_calls == 1);
  assert(g_exit_calls == 0);
  printf("libnet process-lifetime socket initialization: PASS (%s)\n",
      expected == 0 ? (g_initialize_result == 0 ? "initialized" : "already initialized") : "failure retained");
  return 0;
}
