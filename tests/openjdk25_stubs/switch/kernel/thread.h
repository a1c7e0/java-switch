/* Test-only libnx TLS declarations; never a Horizon implementation. */
#pragma once
int threadTlsAlloc(void (*destructor)(void *));
void *threadTlsGet(int slot);
void threadTlsSet(int slot, void *value);
