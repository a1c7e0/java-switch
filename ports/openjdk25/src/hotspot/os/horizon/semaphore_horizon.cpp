/* SPDX-License-Identifier: GPL-2.0-only */
#include "semaphore_horizon.hpp"
#include "utilities/debug.hpp"

HorizonSemaphore::HorizonSemaphore(uint value)
  : _semaphore(js_horizon_semaphore_create(value)) {
  guarantee(_semaphore != nullptr, "Horizon semaphore allocation failed");
}
// Like the upstream semaphore, destruction requires that no waiters remain.
HorizonSemaphore::~HorizonSemaphore() { js_horizon_semaphore_destroy(_semaphore); }
void HorizonSemaphore::signal(uint count) { js_horizon_semaphore_signal(_semaphore, count); }
void HorizonSemaphore::wait() { js_horizon_semaphore_wait(_semaphore); }
bool HorizonSemaphore::trywait() { return js_horizon_semaphore_trywait(_semaphore) != 0; }
