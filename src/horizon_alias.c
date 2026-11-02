/* SPDX-License-Identifier: GPL-2.0-only */
#include <javaswitch/detail/horizon_alias.h>

#include <switch.h>
#include <stdlib.h>

#define JS_ALIAS_PAGE_SIZE ((size_t)4096)

struct js_horizon_alias_reservation {
  struct js_horizon_alias_reservation* next;
  uintptr_t address;
  size_t size;
  VirtmemReservation* libnx_reservation;
  struct js_horizon_alias_split* pending_split;
};

struct js_horizon_alias_split {
  js_horizon_alias_reservation* parent;
  js_horizon_alias_reservation* left;
  js_horizon_alias_reservation* right;
};

/* Lock order for all users is caller-local lock, this coordinator lock, then
 * libnx's virtmem lock. The process-wide lock covers both SDK and HotSpot so
 * their software reservations cannot overlap. It cannot see reservations
 * created by unrelated code that bypasses this private coordinator. */
static Mutex g_alias_lock;
static js_horizon_alias_reservation* g_alias_reservations;

static void set_raw(uint32_t* raw_result, Result result) {
  if (raw_result != NULL) *raw_result = (uint32_t)result;
}

static int align_up(uintptr_t value, size_t alignment, uintptr_t* out) {
  if (value > UINTPTR_MAX - (alignment - 1)) return 0;
  *out = (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
  return 1;
}

static js_horizon_alias_status alias_bounds(uintptr_t* start, uintptr_t* end,
                                             uint32_t* raw_result) {
  u64 base = 0, size = 0, extra = 0;
  Result result = svcGetInfo(&base, InfoType_AliasRegionAddress,
                             CUR_PROCESS_HANDLE, 0);
  set_raw(raw_result, result);
  if (R_FAILED(result)) return JS_HORIZON_ALIAS_SYSTEM;
  result = svcGetInfo(&size, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
  set_raw(raw_result, result);
  if (R_FAILED(result)) return JS_HORIZON_ALIAS_SYSTEM;
  /* Match libnx's optional AliasRegionExtraSize adjustment when available. */
  if (R_SUCCEEDED(svcGetInfo(&extra, InfoType_AliasRegionExtraSize,
                             CUR_PROCESS_HANDLE, 0))) {
    if (extra > size) return JS_HORIZON_ALIAS_SYSTEM;
    size -= extra;
  }
  if (base > UINTPTR_MAX || size > UINTPTR_MAX || size == 0 ||
      base > UINTPTR_MAX - size ||
      (base & (JS_ALIAS_PAGE_SIZE - 1)) != 0 ||
      (size & (JS_ALIAS_PAGE_SIZE - 1)) != 0) {
    return JS_HORIZON_ALIAS_SYSTEM;
  }
  *start = (uintptr_t)base;
  *end = (uintptr_t)(base + size);
  set_raw(raw_result, 0);
  return JS_HORIZON_ALIAS_OK;
}

static uintptr_t owned_overlap_end(uintptr_t start, uintptr_t end) {
  uintptr_t overlap_end = 0;
  for (js_horizon_alias_reservation* item = g_alias_reservations;
       item != NULL; item = item->next) {
    const uintptr_t item_end = item->address + item->size;
    if (start < item_end && item->address < end && item_end > overlap_end)
      overlap_end = item_end;
  }
  return overlap_end;
}

static js_horizon_alias_status find_free_alias(size_t size, size_t alignment,
                                                uintptr_t* address,
                                                uint32_t* raw_result) {
  uintptr_t start, end;
  js_horizon_alias_status status = alias_bounds(&start, &end, raw_result);
  if (status != JS_HORIZON_ALIAS_OK) return status;
  uintptr_t candidate;
  if (!align_up(start, alignment, &candidate)) return JS_HORIZON_ALIAS_NO_MEMORY;

  while (candidate <= end && size <= end - candidate) {
    const uintptr_t candidate_end = candidate + size;
    const uintptr_t occupied = owned_overlap_end(candidate, candidate_end);
    if (occupied != 0) {
      if (!align_up(occupied, alignment, &candidate))
        return JS_HORIZON_ALIAS_NO_MEMORY;
      continue;
    }

    uintptr_t cursor = candidate;
    int retry = 0;
    while (cursor < candidate_end) {
      MemoryInfo info;
      u32 page_info = 0;
      Result result = svcQueryMemory(&info, &page_info, cursor);
      set_raw(raw_result, result);
      if (R_FAILED(result)) return JS_HORIZON_ALIAS_SYSTEM;
      if (info.size == 0 || info.addr > cursor || info.addr > UINTPTR_MAX ||
          info.size > UINTPTR_MAX || info.addr > UINTPTR_MAX - info.size ||
          info.addr + info.size <= cursor) {
        return JS_HORIZON_ALIAS_SYSTEM;
      }
      const uintptr_t span_end = (uintptr_t)(info.addr + info.size);
      if (info.type != MemType_Unmapped) {
        if (!align_up(span_end, alignment, &candidate))
          return JS_HORIZON_ALIAS_NO_MEMORY;
        retry = 1;
        break;
      }
      cursor = span_end < candidate_end ? span_end : candidate_end;
    }
    if (!retry) {
      *address = candidate;
      return JS_HORIZON_ALIAS_OK;
    }
  }
  return JS_HORIZON_ALIAS_NO_MEMORY;
}

js_horizon_alias_status js_horizon_alias_reserve(
    size_t size, size_t alignment, void** address,
    js_horizon_alias_reservation** reservation, uint32_t* raw_result) {
  set_raw(raw_result, 0);
  if (address == NULL || reservation == NULL || size == 0 ||
      (size & (JS_ALIAS_PAGE_SIZE - 1)) != 0 ||
      alignment < JS_ALIAS_PAGE_SIZE || (alignment & (alignment - 1)) != 0 ||
      (alignment & (JS_ALIAS_PAGE_SIZE - 1)) != 0) {
    return JS_HORIZON_ALIAS_INVALID;
  }
  *address = NULL;
  *reservation = NULL;
  js_horizon_alias_reservation* item = calloc(1, sizeof(*item));
  if (item == NULL) return JS_HORIZON_ALIAS_NO_MEMORY;

  mutexLock(&g_alias_lock);
  uintptr_t found = 0;
  js_horizon_alias_status status = find_free_alias(size, alignment, &found,
                                                    raw_result);
  if (status == JS_HORIZON_ALIAS_OK) {
    virtmemLock();
    item->libnx_reservation = virtmemAddReservation((void*)found, size);
    virtmemUnlock();
    if (item->libnx_reservation == NULL) {
      status = JS_HORIZON_ALIAS_NO_MEMORY;
    } else {
      item->address = found;
      item->size = size;
      item->next = g_alias_reservations;
      g_alias_reservations = item;
    }
  }
  mutexUnlock(&g_alias_lock);
  if (status != JS_HORIZON_ALIAS_OK) {
    free(item);
    return status;
  }
  *address = (void*)found;
  *reservation = item;
  return JS_HORIZON_ALIAS_OK;
}

static int unlink_reservation(js_horizon_alias_reservation* reservation) {
  js_horizon_alias_reservation** link = &g_alias_reservations;
  while (*link != NULL && *link != reservation) link = &(*link)->next;
  if (*link == NULL) return 0;
  *link = reservation->next;
  return 1;
}

js_horizon_alias_status js_horizon_alias_release(
    js_horizon_alias_reservation* reservation, uint32_t* raw_result) {
  set_raw(raw_result, 0);
  if (reservation == NULL) return JS_HORIZON_ALIAS_INVALID;
  mutexLock(&g_alias_lock);
  if (reservation->pending_split != NULL ||
      !unlink_reservation(reservation)) {
    mutexUnlock(&g_alias_lock);
    return JS_HORIZON_ALIAS_STATE;
  }
  virtmemLock();
  virtmemRemoveReservation(reservation->libnx_reservation);
  virtmemUnlock();
  mutexUnlock(&g_alias_lock);
  free(reservation);
  return JS_HORIZON_ALIAS_OK;
}

js_horizon_alias_status js_horizon_alias_split_begin(
    js_horizon_alias_reservation* parent, size_t left_size,
    size_t removed_size, size_t right_size, js_horizon_alias_split** split_out,
    uint32_t* raw_result) {
  set_raw(raw_result, 0);
  if (split_out == NULL || parent == NULL || removed_size == 0 ||
      (left_size | removed_size | right_size) & (JS_ALIAS_PAGE_SIZE - 1))
    return JS_HORIZON_ALIAS_INVALID;
  *split_out = NULL;
  if (left_size > SIZE_MAX - removed_size ||
      left_size + removed_size > SIZE_MAX - right_size ||
      left_size + removed_size + right_size != parent->size)
    return JS_HORIZON_ALIAS_INVALID;

  js_horizon_alias_split* split = calloc(1, sizeof(*split));
  js_horizon_alias_reservation* left = left_size ? calloc(1, sizeof(*left)) : NULL;
  js_horizon_alias_reservation* right = right_size ? calloc(1, sizeof(*right)) : NULL;
  if (split == NULL || (left_size && left == NULL) ||
      (right_size && right == NULL)) {
    free(split); free(left); free(right);
    return JS_HORIZON_ALIAS_NO_MEMORY;
  }
  if (left != NULL) { left->address = parent->address; left->size = left_size; }
  if (right != NULL) {
    right->address = parent->address + left_size + removed_size;
    right->size = right_size;
  }
  split->parent = parent;
  split->left = left;
  split->right = right;

  mutexLock(&g_alias_lock);
  int present = 0;
  for (js_horizon_alias_reservation* item = g_alias_reservations;
       item != NULL; item = item->next) if (item == parent) present = 1;
  if (!present || parent->pending_split != NULL) {
    mutexUnlock(&g_alias_lock);
    free(split); free(left); free(right);
    return JS_HORIZON_ALIAS_STATE;
  }
  parent->pending_split = split;
  virtmemLock();
  if (left != NULL)
    left->libnx_reservation = virtmemAddReservation((void*)left->address, left->size);
  if (left != NULL && left->libnx_reservation == NULL) {
    parent->pending_split = NULL;
    virtmemUnlock(); mutexUnlock(&g_alias_lock);
    free(split); free(left); free(right);
    return JS_HORIZON_ALIAS_NO_MEMORY;
  }
  if (right != NULL)
    right->libnx_reservation = virtmemAddReservation((void*)right->address, right->size);
  if (right != NULL && right->libnx_reservation == NULL) {
    if (left != NULL) virtmemRemoveReservation(left->libnx_reservation);
    parent->pending_split = NULL;
    virtmemUnlock(); mutexUnlock(&g_alias_lock);
    free(split); free(left); free(right);
    return JS_HORIZON_ALIAS_NO_MEMORY;
  }
  virtmemUnlock();
  mutexUnlock(&g_alias_lock);
  *split_out = split;
  return JS_HORIZON_ALIAS_OK;
}

void js_horizon_alias_split_commit(js_horizon_alias_split* split,
    js_horizon_alias_reservation** left_out,
    js_horizon_alias_reservation** right_out) {
  if (left_out != NULL) *left_out = NULL;
  if (right_out != NULL) *right_out = NULL;
  if (split == NULL) return;
  if ((split->left != NULL && left_out == NULL) ||
      (split->right != NULL && right_out == NULL)) return;
  mutexLock(&g_alias_lock);
  js_horizon_alias_reservation* parent = split->parent;
  if (parent->pending_split != split) {
    mutexUnlock(&g_alias_lock);
    return;
  }
  virtmemLock();
  (void)unlink_reservation(parent);
  virtmemRemoveReservation(parent->libnx_reservation);
  if (split->left != NULL) {
    split->left->next = g_alias_reservations;
    g_alias_reservations = split->left;
  }
  if (split->right != NULL) {
    split->right->next = g_alias_reservations;
    g_alias_reservations = split->right;
  }
  virtmemUnlock();
  mutexUnlock(&g_alias_lock);
  free(parent);
  if (left_out != NULL) *left_out = split->left;
  if (right_out != NULL) *right_out = split->right;
  free(split);
}

void js_horizon_alias_split_rollback(js_horizon_alias_split* split) {
  if (split == NULL) return;
  mutexLock(&g_alias_lock);
  js_horizon_alias_reservation* parent = split->parent;
  if (parent->pending_split == split) {
    virtmemLock();
    if (split->left != NULL) virtmemRemoveReservation(split->left->libnx_reservation);
    if (split->right != NULL) virtmemRemoveReservation(split->right->libnx_reservation);
    parent->pending_split = NULL;
    virtmemUnlock();
  }
  mutexUnlock(&g_alias_lock);
  free(split->left); free(split->right); free(split);
}
