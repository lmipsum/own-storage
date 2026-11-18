// SPDX-License-Identifier: GPL-3.0-or-later
// The VSH's vshcommon library, as the plugin modules import it by NID (its stubs/ exports.yml):
// vsh::OptionMenu, the side menu of the XMB's Triangle, which is one menu for the whole XMB and
// keeps the open menu's state, whichever view opened it; the system's Yes/No dialog; and the
// screen's button hints.

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
/**
 * Opens a child level of the open menu on another of its page's planes, as explore_plugin opens
 * its submenus: OptionMenu_Open's style, deferred show and label type, no layout, then -1, -1.
 * @return 0 when it opened
 */
int OptionMenu_OpenChild(int view, const char *plane, int style, int deferred_show, int label_type,
                         void *layout, int a6, int a7);
/** Closes the open menu's deepest child level; 0 when it closed. */
int OptionMenu_CloseChild(int view);
/** The open menu's current level: 0 for the first. */
int OptionMenu_GetLevel(void);

/**
 * vsh::MessageDialog's open, as ps3_savedata_plugin asks its delete question: a dialog on a page's
 * widget with a text; type 0 has Yes and No. ps3_savedata_plugin passes 5, 1 and 0x64 for icon,
 * buttons and the last argument. on_answer(result, data) gets 0 for Yes; the dialog then goes by
 * itself.
 * @param text a VSH std::wstring, which the dialog copies
 * @return the dialog; NULL when it did not open
 */
void *MessageDialog_Open(void *parent, const void *text, int type, void *on_answer, int icon,
                         int buttons, void *data, int a7);
/** vsh::MessageDialog::HideDelete: takes an open dialog away. */
void MessageDialog_HideDelete(void *dialog);

/** Shows or hides the Back (O) hint of the screen's button navigation. */
int ShowButtonNavigation_Back(int show);

#endif
