// SPDX-License-Identifier: GPL-3.0-or-later
// Triangle on one of the app's save rows opens the save menu for that save.

#ifndef OWNS_SAVE_MENU_H
#define OWNS_SAVE_MENU_H

/**
 * Opens the save menu for the XMB item the user is on, when it is one of the app's save rows;
 * on any other item, or with the menu open already, nothing happens.
 */
void save_menu_open_for_focused_item(void);

#endif
