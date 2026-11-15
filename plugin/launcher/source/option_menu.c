// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin's Menu::open (4.82), which the calls below follow: View::Activate, OptionMenu's
// Open, the level-0 lists' style, the row count, then the lower list's cursor and focus.
#include "option_menu.h"

#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "paf.h"
#include "vshcommon.h"

// OptionMenu_Open's arguments, explore_plugin's for its own menu.
#define OPEN_STYLE 0x4b4
#define OPEN_SHOW_AT_ONCE 0
#define OPEN_LAYOUT_METRIC 0
#define OPEN_LABEL_TYPE 0x100
#define MENU_LEVEL 0
#define LOWER_LIST 0
#define UPPER_LIST 1
// PhMenuList's style setter (vtable +0xf4) and explore_plugin's arguments to it.
#define LIST_SET_STYLE_SLOT 0xf4
#define LIST_STYLE 0x64
#define LIST_STYLE_MODE 7
#define LIST_STYLE_METRIC 0x47d
// PhMenuList's own SetFocus (vtable +0x130), called with no event and mode 2.
#define LIST_SET_FOCUS_SLOT 0x130
#define LIST_FOCUS_MODE 2

/** A virtual function of a paf object, by its slot in the object's vtable. */
static const void *virtual_function(const void *object, uint32_t slot)
{
    const uint32_t vtable = *(const uint32_t *)object;
    return *(const void *const *)(uintptr_t)(vtable + slot);
}

/** Styles one of the first level's lists as explore_plugin does right after the open. */
static void style_list(int list_type)
{
    void *list = OptionMenu_GetMenuList(MENU_LEVEL, list_type);
    if (list == NULL)
    {
        owns_log_value("no menu list", (uint32_t)list_type);
        return;
    }
    typedef int (*set_style)(void *list, int style, int mode, int metric, float value);
    const set_style set = (set_style)virtual_function(list, LIST_SET_STYLE_SLOT);
    set(list, LIST_STYLE, LIST_STYLE_MODE, LIST_STYLE_METRIC, 0.0f);
}

/** Puts the cursor on the first row and gives the lower list the input. */
static void focus_first_row(void)
{
    void *list = OptionMenu_GetMenuList(MENU_LEVEL, LOWER_LIST);
    if (list == NULL)
    {
        owns_log("no lower menu list");
        return;
    }
    PhMenuList_SetSelected(list, 0);
    typedef void (*set_focus)(void *widget, void *event, unsigned int mode);
    const set_focus focus = (set_focus)virtual_function(list, LIST_SET_FOCUS_SLOT);
    focus(list, NULL, LIST_FOCUS_MODE);
}

int option_menu_open(int view)
{
    // OptionMenu may keep the array, as explore_plugin's is static.
    static const char *const kPlanes[] = {"plane_menu0", "plane_menu1", "plane_menu2"};
    // The menu shows on the active view: without this it opens, but is not drawn.
    View_Activate(view);
    const int result = OptionMenu_Open(view, OWNS_MENU_PAGE, kPlanes, OPEN_STYLE, OPEN_SHOW_AT_ONCE,
                                       OPEN_LAYOUT_METRIC, OPEN_LABEL_TYPE,
                                       sizeof kPlanes / sizeof kPlanes[0], NULL);
    owns_log_value("option menu opened", (uint32_t)result);
    if (result != 0)
    {
        return 0;
    }
    // Upper, then lower, in explore_plugin's order.
    style_list(UPPER_LIST);
    style_list(LOWER_LIST);
    OptionMenu_SetItemNum(OWNS_MENU_ROWS, LOWER_LIST, 0);
    focus_first_row();
    return 1;
}
