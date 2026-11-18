// SPDX-License-Identifier: GPL-3.0-or-later
#include "item_menu.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>

#include "list_switch.h"
#include "menu_target.h"
#include "menu_view.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "row_index.h"
#include "vsh_types.h"
#include "xmb_focus.h"

// Room for an item's title and description as UTF-8.
#define TITLE_BYTES 256
#define INFO_BYTES 512

/** Where the list the user is on is, by the list file it was opened from, and its view. */
static enum menu_place focused_place(xmb_view *shown)
{
    switch (list_switch_current(shown))
    {
    case LIST_FILE_MENU:
        return MENU_PLACE_CLOUD;
    case LIST_FILE_SAVES:
        return MENU_PLACE_GAME_FOLDER;
    default:
        return MENU_PLACE_OTHER;
    }
}

void item_menu_open_for_focused_item(void)
{
    // The target is there from the menu's request until its end: a second Triangle while the view
    // still loads must not load it again.
    if (menu_view_loaded() || owns_file_exists(OWNS_MENU_TARGET))
    {
        return;
    }
    void *item = xmb_focused_item();
    if (item == NULL)
    {
        return;
    }
    // A row is known by its title and description together, as the app's index has them.
    char title[TITLE_BYTES];
    char info[INFO_BYTES];
    xmb_item_text(item, ITEM_TITLE_PART, title, sizeof title);
    xmb_item_text(item, ITEM_INFO_PART, info, sizeof info);
    char *record = row_index_find(title, info);
    if (record == NULL)
    {
        return;
    }
    xmb_view shown;
    const enum menu_place place = focused_place(&shown);
    const int written = menu_target_write(place, shown, record);
    vsh_delete(record);
    if (written && !menu_view_load())
    {
        cellFsUnlink(OWNS_MENU_TARGET);
    }
}
