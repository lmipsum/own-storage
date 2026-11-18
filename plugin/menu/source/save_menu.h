// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's rows for one of the app's save rows: its information and its delete.

#ifndef OWNS_SAVE_MENU_H
#define OWNS_SAVE_MENU_H

#include "menu_finish.h"
#include "menu_rows.h"
#include "menu_target.h"

/**
 * Runs a save's lower-list row, the option menu closed already: Information shows the save's
 * information page; Delete asks first, then has the app delete the save in the cloud.
 * @param target the save's, a ROW_RECORD_SAVE record
 * @return how the menu ends; MENU_END_PAGE_OPEN while a page shows
 */
enum menu_end save_menu_run(int view, enum menu_action action, const menu_target *target);

#endif
