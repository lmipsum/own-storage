// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's full-screen information page, as explore_plugin's Information shows a save:
// a label and a value a row, over the XMB's wallpaper.

#ifndef OWNS_INFO_PAGE_H
#define OWNS_INFO_PAGE_H

#include "menu_text.h"
#include "xmb_backdrop.h"

/** A row of the page: its label, and its value as UTF-8, or as UTF-16 when value_units is set. */
typedef struct
{
    menu_label label;
    const char *value;
    const unsigned short *value_units;
} info_row;

/**
 * Opens the page on the view with its rows, the XMB behind it as the backdrop says, and its list
 * focused; the page's Owns::cbInfoCancel, or a question's answer, closes it.
 * @return 1 when it opened; else nothing is shown
 */
int info_page_open(int view, const info_row *rows, int count, enum xmb_backdrop backdrop);

#endif
