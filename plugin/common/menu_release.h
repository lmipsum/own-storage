// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's view cannot unload itself from within its own handlers; the launcher's thread
// unloads it when asked. The launcher, which stays loaded, also switches the XMB's list to a new
// view then, as explore_plugin may still read the command it is handed after the view is gone.

#ifndef OWNS_MENU_RELEASE_H
#define OWNS_MENU_RELEASE_H

/** What the launcher does once the view is unloaded. */
enum menu_release_then
{
    MENU_RELEASE_ONLY,
    /** Switches the open list to the view the user chose (list_switch.h). */
    MENU_RELEASE_SWITCH_LIST,
};

/** Asks the launcher to unload the save menu's view, whose menu and pages are closed. */
void menu_release_request(enum menu_release_then then);

/**
 * Takes the view's request, if there is one.
 * @param then set to what is to follow the unload
 * @return 1 when the view asked to be unloaded
 */
int menu_release_take(enum menu_release_then *then);

#endif
