// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_menu.h"

#include <stdint.h>

#include "menu_view.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "xmb_focus.h"
#include "xmb_rows.h"

// Room for an item's title and info line as UTF-8, and for a save folder's name.
#define TITLE_BYTES 256
#define INFO_BYTES 512
#define SAVE_NAME_BYTES 64

void save_menu_open_for_focused_item(void)
{
    if (menu_view_loaded())
    {
        return;
    }
    void *item = xmb_focused_item();
    if (item == NULL)
    {
        return;
    }
    // A save row is known by its title and info line together, as the app lists them.
    char title[TITLE_BYTES];
    char info[INFO_BYTES];
    xmb_item_text(item, ITEM_TITLE_PART, title, sizeof title);
    xmb_item_text(item, ITEM_INFO_PART, info, sizeof info);
    char key[SAVE_ROW_KEY_BYTES];
    const int matches = xmb_rows_find_save(title, info, key, sizeof key);
    owns_log_value("save rows matching", (uint32_t)matches);
    if (matches != 1)
    {
        return;
    }
    char name[SAVE_NAME_BYTES];
    if (!xmb_rows_save_name(key, name, sizeof name))
    {
        owns_log("save row without a script");
        return;
    }
    // The menu's view reads it when a row is pushed.
    if (!owns_write_file(OWNS_MENU_TARGET, name))
    {
        owns_log("target not written");
        return;
    }
    menu_view_load();
}
