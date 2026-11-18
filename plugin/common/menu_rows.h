// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's rows, as the Saved Data Utility's menu has them: on the first level's lower list
// what the row's save or game can do; on its upper list the sections that change the list's view,
// each opening a submenu of its choices on that submenu's upper list.

#ifndef OWNS_MENU_ROWS_H
#define OWNS_MENU_ROWS_H

#include <stdint.h>

#include "menu_target.h"

/** What a lower-list row does. */
enum menu_action
{
    MENU_ACTION_INFO,
    MENU_ACTION_DELETE,
    MENU_ACTION_DOWNLOAD_ALL,
    MENU_ACTION_DELETE_ALL,
    MENU_ACTIONS
};

/** An upper-list row: the submenu it opens. */
enum menu_section
{
    MENU_SECTION_SORT,
    MENU_SECTION_GROUP,
    MENU_SECTIONS
};

/** The lower list's row count for a kind of row (ROW_RECORD_SAVE or ROW_RECORD_GAME). */
int menu_action_count(char kind);

/** A lower-list row's action; MENU_ACTIONS past the last. */
enum menu_action menu_action_at(char kind, uint32_t row);

/** The upper list's row count for where the row's list is. */
int menu_section_count(enum menu_place place);

/** An upper-list row's section; MENU_SECTIONS past the last. */
enum menu_section menu_section_at(enum menu_place place, uint32_t row);

/** A section's submenu row count. */
int menu_choice_count(enum menu_section section);

#endif
