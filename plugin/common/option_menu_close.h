// SPDX-License-Identifier: GPL-3.0-or-later
// Closing the save menu's option menu from whichever of its levels is open, the same way for every
// path that ends the menu.

#ifndef OWNS_OPTION_MENU_CLOSE_H
#define OWNS_OPTION_MENU_CLOSE_H

/**
 * Closes the open submenus, the deepest first as O closes them, then the menu.
 * @return OptionMenu_Close's result: 0 when it closed
 */
int option_menu_close(int view);

#endif
