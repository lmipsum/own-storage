// SPDX-License-Identifier: GPL-3.0-or-later
#include "option_menu_close.h"

#include <stddef.h>

#include "owns_paths.h"
#include "vshcommon.h"

// The option menu's levels: the first and its submenus (plane_menu0..2).
#define MENU_LEVELS 3

int option_menu_close(int view)
{
    // A level beyond the menu's is none of ours, and is left alone.
    for (int level = OptionMenu_GetLevel(); level > 0 && level < MENU_LEVELS; --level)
    {
        OptionMenu_CloseChild(view);
    }
    return OptionMenu_Close(view, OWNS_MENU_PAGE, NULL);
}
