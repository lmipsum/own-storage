// SPDX-License-Identifier: GPL-3.0-or-later
// Triangle on one of the app's save or game folder rows opens the save menu for that row.

#ifndef OWNS_ITEM_MENU_H
#define OWNS_ITEM_MENU_H

/**
 * Opens the save menu for the XMB item the user is on, when it is one of the app's save or game
 * folder rows; on any other item, or with the menu open already, nothing happens.
 */
void item_menu_open_for_focused_item(void);

#endif
