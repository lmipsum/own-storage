// SPDX-License-Identifier: GPL-3.0-or-later
// Setting a text part of a paf widget, as explore_plugin sets its lists' row texts.

#ifndef OWNS_WIDGET_TEXT_H
#define OWNS_WIDGET_TEXT_H

// The most units widget_set_text passes on; more are left out.
#define WIDGET_TEXT_UNITS 128

/**
 * Sets one of a widget's text parts through its SetText (vtable +0x118), which copies the text.
 * @param part the part's id; a list's row part is (row << 16) | the part
 * @param text UTF-16, NUL-ended; more than WIDGET_TEXT_UNITS - 1 units are left out
 */
void widget_set_text(void *widget, int part, const unsigned short *text);

/**
 * As widget_set_text, for a UTF-8 text of any length, converted on the VSH's heap.
 * @return 0 when the heap had no room, and the part is unchanged
 */
int widget_set_utf8(void *widget, int part, const char *text);

#endif
