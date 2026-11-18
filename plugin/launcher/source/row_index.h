// SPDX-License-Identifier: GPL-3.0-or-later
// Which of the app's XMB rows an item is, by the app's row index (OWNS_ROWS_FILE, row_record.h):
// the row whose title and description are the item's.

#ifndef OWNS_ROW_INDEX_H
#define OWNS_ROW_INDEX_H

/**
 * The record of the one game or save row with this title and description.
 * @param title the item's title, UTF-8
 * @param info the item's description, UTF-8
 * @return a copy of the record as the index has it, NUL-ended, on the VSH's heap (vsh_delete);
 *         NULL when not exactly one row matches, or the index cannot be read
 */
char *row_index_find(const char *title, const char *info);

#endif
