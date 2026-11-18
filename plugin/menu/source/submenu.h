// SPDX-License-Identifier: GPL-3.0-or-later
// The option menu's levels, as explore_plugin's Menu moves between them for the Saved Data
// Utility's Sort By and Group Content: a section's submenu opens on the next plane, its choices on
// its upper list; O or Left closes it again.

#ifndef OWNS_SUBMENU_H
#define OWNS_SUBMENU_H

#include <stdint.h>

#include "menu_rows.h"
#include "menu_target.h"

// A level's lists, as OptionMenu_GetMenuList takes them.
#define SUBMENU_LOWER_LIST 0
#define SUBMENU_UPPER_LIST 1

/** The open menu's current level: 0 for the first, else a submenu's. */
uint32_t submenu_level(void);

/** The first level's upper-list row the open submenu belongs to: that list's selected row. */
uint32_t submenu_parent_row(void);

/**
 * Where a menu list is.
 * @param level set to its level
 * @param upper set to whether it is its level's upper list
 * @return 0 when it is none of the menu's lists
 */
int submenu_list_place(const void *list, uint32_t *level, int *upper);

/** The selected row of a menu list, as cbPush finds it. */
uint32_t submenu_selected_row(const void *list);

/** Gives one of a level's lists the input, its cursor on a row (left as it is when row < 0). */
void submenu_focus(uint32_t level, int list_type, int row);

/**
 * Opens a section's submenu on the next level, the cursor on the current choice.
 * @param head the target's, whose shown view gives the current choice
 */
void submenu_open(int view, enum menu_section section, const menu_head *head);

/** Closes the open submenu and gives its parent's upper list the input again. */
void submenu_close(int view, uint32_t level);

#endif
