// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_text.h"

#include <stddef.h>

#include "owns_paths.h"
#include "paf.h"

/** A plugin's text by its RCO label; NULL when the plugin is not loaded or lacks it. */
static const unsigned short *plugin_text(const char *plugin, const char *label)
{
    const int view = View_Find(plugin);
    return view != 0 ? View_GetString(view, label) : NULL;
}

const unsigned short *menu_text(const menu_label *label)
{
    const unsigned short *text =
        label->borrowed ? plugin_text("explore_plugin", label->label) : NULL;
    return text != NULL ? text : plugin_text(OWNS_MENU_VIEW, label->label);
}
