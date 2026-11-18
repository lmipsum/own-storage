// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's plugin view (plugin/menu): loaded with its RCO as xmb_plugin loads its plugins,
// so that its pages can open as the option menu and full screen; unloaded again once they closed.

#ifndef OWNS_MENU_VIEW_H
#define OWNS_MENU_VIEW_H

/** Whether the save menu's view is loaded. */
int menu_view_loaded(void);

/**
 * Has paf load the save menu's view, which then opens its menu for the target the launcher left.
 * @return 0 when the load was not asked for, as its files are missing
 */
int menu_view_load(void);

/**
 * Unloads the save menu's view when it asked for it, once its menu and pages closed, then
 * switches the XMB's list to the chosen view when it asked for that too.
 */
void menu_view_take_release(void);

#endif
