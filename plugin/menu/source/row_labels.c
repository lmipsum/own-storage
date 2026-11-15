// SPDX-License-Identifier: GPL-3.0-or-later
// A PhList row's label is part 0xc8 of row (index << 16), as explore_plugin's Menu sets it; the
// list's SetText (vtable +0x118, PhWidget::SetText's slot) passes it to that row.
#include "row_labels.h"

#include <stddef.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "paf.h"
#include "vsh_types.h"

#define ROW_LABEL_PART 0xc8
#define SET_TEXT_SLOT 0x118
// The most characters of a row's label; more are left out.
#define ROW_LABEL_UNITS 64

/** A row's label: a plugin's text, else the save menu's own in its RCO (owns_menu.json). */
typedef struct
{
    const char *plugin;
    const char *label;
    const char *own_label;
} row_label;

static const row_label kRows[OWNS_MENU_ROWS] = {
    [OWNS_ROW_INFO] = {"explore_plugin", "msg_information", "msg_owns_info"},
    [OWNS_ROW_DELETE] = {"explore_plugin", "msg_del", "msg_owns_delete"},
};

/** A plugin's text, if the plugin is loaded and has it. */
static const unsigned short *plugin_text(const char *plugin, const char *label)
{
    const int view = View_Find(plugin);
    return view != 0 ? View_GetString(view, label) : NULL;
}

void row_labels_set(void *list, uint32_t row)
{
    if (row >= OWNS_MENU_ROWS)
    {
        return;
    }
    const unsigned short *text = plugin_text(kRows[row].plugin, kRows[row].label);
    if (text == NULL)
    {
        text = plugin_text(OWNS_MENU_VIEW, kRows[row].own_label);
    }
    if (text == NULL)
    {
        owns_log_value("no label for row", row);
        return;
    }
    unsigned short units[ROW_LABEL_UNITS];
    uint32_t length = 0;
    for (; text[length] != 0 && length + 1 < ROW_LABEL_UNITS; ++length)
    {
        units[length] = text[length];
    }
    units[length] = 0;
    // SetText copies it, as explore_plugin destroys its own right after the call.
    vsh_wstring label = {0, {{0}}, length, VSH_WSTRING_IN_PLACE};
    if (length <= VSH_WSTRING_IN_PLACE)
    {
        for (uint32_t i = 0; i <= length; ++i)
        {
            label.data.text[i] = units[i];
        }
    }
    else
    {
        label.data.heap = units;
        label.capacity = length;
    }
    typedef int (*set_text)(void *widget, const vsh_wstring *text, int part);
    const uint32_t vtable = *(const uint32_t *)list;
    const set_text set = *(const set_text *)(uintptr_t)(vtable + SET_TEXT_SLOT);
    set(list, &label, (int)((row << 16) | ROW_LABEL_PART));
}
