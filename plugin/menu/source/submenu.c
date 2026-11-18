// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin's Menu (4.82/4.88) opens a submenu (0x5ecf0) with OptionMenu's OpenChild and
// OptionMenu_Open's arguments, sets the child's rows on its upper list and none on its lower one
// (0x5e694), and focuses the upper list (0x10f55c); closing it (0x5e7f8) gives the parent's upper
// list the input again.
#include "submenu.h"

#include <stddef.h>

#include "owns_log.h"
#include "paf.h"
#include "view_choice.h"
#include "vshcommon.h"
#include "vtable.h"

// The option menu's levels (plane_menu0..2), OptionMenu_Open's arguments again, the row cbPush
// pushed (the list's +0x510), and PhMenuList's SetFocus (vtable +0x130) with no event and mode 2.
#define MENU_LEVELS 3
#define OPEN_STYLE 0x4b4
#define OPEN_SHOW_AT_ONCE 0
#define OPEN_LABEL_TYPE 0x100
#define LIST_SELECTED_ROW 0x510
#define LIST_SET_FOCUS_SLOT 0x130
#define LIST_FOCUS_MODE 2

uint32_t submenu_level(void)
{
    const int level = OptionMenu_GetLevel();
    return level > 0 ? (uint32_t)level : 0;
}

uint32_t submenu_selected_row(const void *list)
{
    return object_word(list, LIST_SELECTED_ROW);
}

uint32_t submenu_parent_row(void)
{
    const void *list = OptionMenu_GetMenuList(0, SUBMENU_UPPER_LIST);
    return list != NULL ? submenu_selected_row(list) : MENU_SECTIONS;
}

int submenu_list_place(const void *list, uint32_t *level, int *upper)
{
    for (int at = 0; at < MENU_LEVELS; ++at)
    {
        const void *upper_list = OptionMenu_GetMenuList(at, SUBMENU_UPPER_LIST);
        if (OptionMenu_GetMenuList(at, SUBMENU_LOWER_LIST) == list || upper_list == list)
        {
            *level = (uint32_t)at;
            *upper = upper_list == list;
            return 1;
        }
    }
    return 0;
}

void submenu_focus(uint32_t level, int list_type, int row)
{
    void *list = OptionMenu_GetMenuList((int)level, list_type);
    if (list == NULL)
    {
        owns_log_value("no list at level", level);
        return;
    }
    if (row >= 0)
    {
        PhMenuList_SetSelected(list, row);
    }
    typedef void (*set_focus)(void *widget, void *event, unsigned int mode);
    ((set_focus)virtual_function(list, LIST_SET_FOCUS_SLOT))(list, NULL, LIST_FOCUS_MODE);
}

void submenu_open(int view, enum menu_section section, const menu_head *head)
{
    // OptionMenu may keep the plane's name, as explore_plugin's are static.
    static const char *const kPlanes[MENU_LEVELS] = {"plane_menu0", "plane_menu1", "plane_menu2"};
    const uint32_t level = submenu_level() + 1;
    const int count = menu_choice_count(section);
    if (count <= 0 || level >= MENU_LEVELS)
    {
        return;
    }
    const int result = OptionMenu_OpenChild(view, kPlanes[level], OPEN_STYLE, OPEN_SHOW_AT_ONCE,
                                            OPEN_LABEL_TYPE, NULL, -1, -1);
    owns_log_value("submenu opened", (uint32_t)result);
    if (result != 0)
    {
        return;
    }
    const int current = (int)view_choice_current(section, head);
    OptionMenu_SetItemNum(0, SUBMENU_LOWER_LIST, 0);
    OptionMenu_SetItemNum(count, SUBMENU_UPPER_LIST, current);
    submenu_focus(submenu_level(), SUBMENU_UPPER_LIST, current);
}

void submenu_close(int view, uint32_t level)
{
    const int result = OptionMenu_CloseChild(view);
    owns_log_value("submenu closed", (uint32_t)result);
    if (result == 0 && level > 0)
    {
        submenu_focus(level - 1, SUBMENU_UPPER_LIST, -1);
    }
}
