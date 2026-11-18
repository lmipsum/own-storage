// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's row labels: its first level's lower list (what the row can do) and upper list
// (the list's sections), and the Sort By and Group Content submenus, which show on their upper
// list as the Saved Data Utility's do.

#ifndef OWNS_ROW_LABELS_H
#define OWNS_ROW_LABELS_H

#include <stdint.h>

#include "menu_rows.h"
#include "menu_target.h"

/**
 * Sets a row's label on the menu list that brings the row in.
 * @param list the list, as its cbScrollIn event gets it
 * @param level 0 for the first level, else a submenu's
 * @param upper whether the list is its level's upper one
 * @param section the submenu's section; ignored on the first level
 */
void row_labels_set(void *list, uint32_t level, int upper, enum menu_section section,
                    enum menu_place place, char kind, uint32_t row);

#endif
