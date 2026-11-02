/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JAVASWITCH_DETAIL_HORIZON_ALIAS_H
#define JAVASWITCH_DETAIL_HORIZON_ALIAS_H

#include <stddef.h>
#include <stdint.h>

/* Internal process-wide coordinator for SDK and HotSpot physical mappings.
 * This is not part of the JavaSwitch ABI 1 public API. */
typedef struct js_horizon_alias_reservation js_horizon_alias_reservation;
typedef struct js_horizon_alias_split js_horizon_alias_split;

typedef enum js_horizon_alias_status {
  JS_HORIZON_ALIAS_OK = 0,
  JS_HORIZON_ALIAS_INVALID,
  JS_HORIZON_ALIAS_STATE,
  JS_HORIZON_ALIAS_NO_MEMORY,
  JS_HORIZON_ALIAS_SYSTEM
} js_horizon_alias_status;

js_horizon_alias_status js_horizon_alias_reserve(
    size_t size, size_t alignment, void** address,
    js_horizon_alias_reservation** reservation, uint32_t* raw_result);

/* Release only after the caller has unmapped every committed page. */
js_horizon_alias_status js_horizon_alias_release(
    js_horizon_alias_reservation* reservation, uint32_t* raw_result);

/* Replace one live range with left/right child reservations. The parent stays
 * reserved until commit; rollback removes children and restores the parent.
 * Commit must receive a non-NULL output for each non-empty child. */
js_horizon_alias_status js_horizon_alias_split_begin(
    js_horizon_alias_reservation* parent, size_t left_size,
    size_t removed_size, size_t right_size, js_horizon_alias_split** split,
    uint32_t* raw_result);
void js_horizon_alias_split_commit(js_horizon_alias_split* split,
    js_horizon_alias_reservation** left,
    js_horizon_alias_reservation** right);
void js_horizon_alias_split_rollback(js_horizon_alias_split* split);

#endif
