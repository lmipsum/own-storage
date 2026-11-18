// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's rows for one of the app's game folder rows: the folder's information, and its
// saves in the cloud downloaded or deleted at once.

#ifndef OWNS_GAME_MENU_H
#define OWNS_GAME_MENU_H

#include "menu_finish.h"
#include "menu_rows.h"
#include "menu_target.h"

/**
 * Runs a game folder's lower-list row, the option menu closed already: Information shows the
 * folder's information page; Download All has the app download the game's saves; Delete All asks
 * first, then has the app delete them in the cloud.
 * @param target the folder's, a ROW_RECORD_GAME record
 * @return how the menu ends; MENU_END_PAGE_OPEN while a page shows
 */
enum menu_end game_menu_run(int view, enum menu_action action, const menu_target *target);

#endif
