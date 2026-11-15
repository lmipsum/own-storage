// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's view cannot unload itself from within its own handlers; the launcher's thread
// unloads it when asked.

#ifndef OWNS_MENU_RELEASE_H
#define OWNS_MENU_RELEASE_H

/** Asks the launcher to unload the save menu's view, whose menu is closed. */
void menu_release_request(void);

#endif
