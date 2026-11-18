// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's texts. Where explore_plugin has the words the XMB shows for its own saved data,
// they are borrowed, in the system's language; the save menu's RCO (owns_menu.json) has an English
// copy under the same label for a system without them, and the texts the system has none like.

#ifndef OWNS_MENU_TEXT_H
#define OWNS_MENU_TEXT_H

/** A text of the save menu by its label. */
typedef struct
{
    const char *label;
    /** Non-zero when explore_plugin has the label, whose text comes first. */
    int borrowed;
} menu_label;

/** A text by its label: explore_plugin's when borrowed, else the save menu's own; NULL without. */
const unsigned short *menu_text(const menu_label *label);

#endif
