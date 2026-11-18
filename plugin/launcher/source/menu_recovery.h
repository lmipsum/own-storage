// SPDX-License-Identifier: GPL-3.0-or-later
// A way back to the XMB should the save menu ever keep the input: the recovery request
// (OWNS_MENU_REFOCUS) has the launcher close whatever the menu has open.

#ifndef OWNS_MENU_RECOVERY_H
#define OWNS_MENU_RECOVERY_H

/**
 * Takes away the question's dialog if one is open, closes the menu with its submenus and the
 * view's pages, puts the XMB's looks back, gives the XMB the input, and has the view unloaded on
 * the launcher's next round.
 */
void menu_recover(void);

#endif
