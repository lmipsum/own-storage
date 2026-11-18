// SPDX-License-Identifier: GPL-3.0-or-later
// The Sort By and Group Content submenus' choices: their rows are the view's letters in order
// (xmb_view.h). The current choice is the view the row's list shows, by its list file's name; the
// file of a list's own name holds whatever view the app wrote last, which the view kept for the
// app stands in for.

#ifndef OWNS_VIEW_CHOICE_H
#define OWNS_VIEW_CHOICE_H

#include <stdint.h>

#include "menu_rows.h"
#include "menu_target.h"

/** The submenu row of a section's current choice. */
uint32_t view_choice_current(enum menu_section section, const menu_head *head);

/**
 * Keeps a submenu row's choice for the app, with the rest of the view the list shows; in a game's
 * folder the group is by title, the folders' own.
 * @return 1 when it was kept, and the list is to switch to it
 */
int view_choice_take(enum menu_section section, const menu_head *head, uint32_t row);

#endif
