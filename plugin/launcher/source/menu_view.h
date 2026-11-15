// SPDX-License-Identifier: GPL-3.0-or-later
// The save menu's plugin view (plugin/menu): loaded with its RCO as xmb_plugin loads its plugins,
// so that its page can open as the option menu; unloaded again once the menu has closed.

#ifndef OWNS_MENU_VIEW_H
#define OWNS_MENU_VIEW_H

/** Whether the save menu's view is loaded. */
int menu_view_loaded(void);

/**
 * Has paf load the save menu's view, which then opens its menu; nothing when its files are
 * missing.
 */
void menu_view_load(void);

/** Unloads the save menu's view when it asked for it, once its menu closed. */
void menu_view_take_release(void);

#endif
