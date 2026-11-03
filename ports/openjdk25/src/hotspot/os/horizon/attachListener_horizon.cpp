/* SPDX-License-Identifier: GPL-2.0-only */
#include "services/attachListener.hpp"

#include <errno.h>

#if INCLUDE_SERVICES

// Horizon has no process-attach transport. os::init_2 defaults
// DisableAttachMechanism to true and rejects explicit requests to enable it;
// these platform hooks remain defensive if the generic listener is reached.
void AttachListener::vm_start() {
}

int AttachListener::pd_init() {
  errno = ENOTSUP;
  return -1;
}

bool AttachListener::check_socket_file() {
  return false;
}

bool AttachListener::init_at_startup() {
  return false;
}

bool AttachListener::is_init_trigger() {
  return false;
}

void AttachListener::abort() {
}

void AttachListener::pd_detachall() {
}

void AttachListener::pd_data_dump() {
}

AttachOperation* AttachListener::dequeue() {
  errno = ENOTSUP;
  return nullptr;
}

#endif // INCLUDE_SERVICES
