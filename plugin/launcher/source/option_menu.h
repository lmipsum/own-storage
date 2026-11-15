// SPDX-License-Identifier: GPL-3.0-or-later
// Opening the save menu's page as the XMB's option menu, the side menu Triangle opens.

#ifndef OWNS_OPTION_MENU_H
#define OWNS_OPTION_MENU_H

/**
 * Opens the save menu on the loaded view, its rows set and the cursor on the first, as
 * explore_plugin opens its own menu.
 * @param view the save menu's view
 * @return 1 when it opened
 */
int option_menu_open(int view);

#endif
