// SPDX-License-Identifier: GPL-3.0-or-later
// The XMB item the user is on, read from explore_plugin's widgets without changing them.

#ifndef OWNS_XMB_FOCUS_H
#define OWNS_XMB_FOCUS_H

#include <stddef.h>

// An XMB item's title and info line, as explore_plugin reads them.
#define ITEM_TITLE_PART 4
#define ITEM_INFO_PART 5

/** The focused item of the focused folder level; NULL when there is none or it is not found. */
void *xmb_focused_item(void);

/**
 * One of an item's texts as NUL-ended UTF-8, cut to fit.
 * @param part ITEM_TITLE_PART or ITEM_INFO_PART
 * @return its bytes
 */
size_t xmb_item_text(void *item, int part, char *text, size_t room);

#endif
