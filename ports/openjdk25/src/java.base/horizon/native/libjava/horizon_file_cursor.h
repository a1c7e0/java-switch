/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
#ifndef HORIZON_FILE_CURSOR_H
#define HORIZON_FILE_CURSOR_H

/* Serializes operations which use or temporarily adjust a shared file cursor.
 * This is process-wide so Java streams and NIO channels coordinate even when
 * they refer to aliases of the same open file description. */
int js_horizon_file_cursor_lock(void);
void js_horizon_file_cursor_unlock(void);

#endif
