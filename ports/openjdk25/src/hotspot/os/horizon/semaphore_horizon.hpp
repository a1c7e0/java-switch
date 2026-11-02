/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_SEMAPHORE_HORIZON_HPP
#define OS_HORIZON_SEMAPHORE_HORIZON_HPP
#include "memory/allocation.hpp"
#include "horizon_bridge.h"

class HorizonSemaphore : public CHeapObj<mtInternal> {
  js_horizon_semaphore* _semaphore;
  NONCOPYABLE(HorizonSemaphore);
public:
  explicit HorizonSemaphore(uint value = 0);
  ~HorizonSemaphore();
  void signal(uint count = 1);
  void wait();
  bool trywait();
};
typedef HorizonSemaphore SemaphoreImpl;
#endif
