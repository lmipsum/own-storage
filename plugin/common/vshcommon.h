// SPDX-License-Identifier: GPL-3.0-or-later
// vsh::OptionMenu of the VSH's vshcommon library, the side menu of the XMB's Triangle, as the
// plugin modules import it by NID (stubs/vshcommon/exports.yml). It is one menu for the whole
// XMB: it keeps the open menu's state, whichever view opened it.

#ifndef OWNS_VSHCOMMON_H
#define OWNS_VSHCOMMON_H

/**
 * Opens a view's page as the option menu, as explore_plugin opens its "expage_menu".
 * @param planes the page's planes, one a menu level; the menu may keep the array
 * @param style explore_plugin's 0x4b4, whose meaning is not known
 * @param deferred_show non-zero keeps the menu hidden until it is shown separately
 * @param layout_metric explore_plugin's 0
 * @param label_type explore_plugin's 0x100
 * @param plane_count the planes' count
 * @param layout explore_plugin's NULL
 * @return 0 when it opened
 */
int OptionMenu_Open(int view, const char *page, const char *const *planes, int style,
                    int deferred_show, int layout_metric, int label_type, int plane_count,
                    void *layout);
/** Closes the option menu that a view's page shows. */
int OptionMenu_Close(int view, const char *page, void *unused);
/** Sets the open menu's row count and selected row; list_type 0 is the lower list. */
int OptionMenu_SetItemNum(int count, int list_type, int selected);
/** One of the open menu's lists: level 0 is the first; list_type 0 lower, 1 upper. */
void *OptionMenu_GetMenuList(int level, int list_type);

#endif
