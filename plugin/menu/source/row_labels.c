// SPDX-License-Identifier: GPL-3.0-or-later
// A PhList row's label is part 0xc8 of row (index << 16), as explore_plugin's Menu sets it; the
// list's SetText passes it to that row. The labels are explore_plugin's own for its saved data
// (explore_plugin_full.rco, 4.82/4.88) where it has them.
#include "row_labels.h"

#include <stddef.h>

#include "menu_text.h"
#include "owns_log.h"
#include "widget_text.h"
#include "xmb_view.h"

#define ROW_LABEL_PART 0xc8

static const menu_label kActionLabels[MENU_ACTIONS] = {
    [MENU_ACTION_INFO] = {"msg_info", 1},
    [MENU_ACTION_DELETE] = {"msg_del", 1},
    [MENU_ACTION_DOWNLOAD_ALL] = {"msg_owns_download_all", 0},
    [MENU_ACTION_DELETE_ALL] = {"msg_owns_delete_all", 0},
};

// "_with_icon" carries the mark of a row that opens a submenu.
static const menu_label kSectionLabels[MENU_SECTIONS] = {
    [MENU_SECTION_SORT] = {"msg_sort", 1},
    [MENU_SECTION_GROUP] = {"msg_folder_class_with_icon", 1},
};

// In XMB_VIEW_SORT_LETTERS' and XMB_VIEW_GROUP_LETTERS' order.
static const menu_label kSortLabels[] = {
    {"msg_updated_date_r", 1},
    {"msg_updated_date_f", 1},
    {"msg_title_f", 1},
    {"msg_subtitle_f", 1},
};
static const menu_label kGroupLabels[] = {
    {"msg_title_folder", 1},
    {"msg_all", 1},
};

_Static_assert(sizeof kSortLabels / sizeof kSortLabels[0] == XMB_VIEW_SORTS, "a label a sort");
_Static_assert(sizeof kGroupLabels / sizeof kGroupLabels[0] == XMB_VIEW_GROUPS, "a label a group");

/** A row's label; NULL when the list has no such row. */
static const menu_label *row_label(uint32_t level, int upper, enum menu_section section,
                                   enum menu_place place, char kind, uint32_t row)
{
    if (level == 0)
    {
        if (upper)
        {
            const enum menu_section at = menu_section_at(place, row);
            return at < MENU_SECTIONS ? &kSectionLabels[at] : NULL;
        }
        const enum menu_action action = menu_action_at(kind, row);
        return action < MENU_ACTIONS ? &kActionLabels[action] : NULL;
    }
    if (!upper || row >= (uint32_t)menu_choice_count(section))
    {
        return NULL;
    }
    return section == MENU_SECTION_SORT ? &kSortLabels[row] : &kGroupLabels[row];
}

void row_labels_set(void *list, uint32_t level, int upper, enum menu_section section,
                    enum menu_place place, char kind, uint32_t row)
{
    const menu_label *label = row_label(level, upper, section, place, kind, row);
    const unsigned short *text = label != NULL ? menu_text(label) : NULL;
    if (text == NULL)
    {
        owns_log_value("no label for row", row);
        return;
    }
    widget_set_text(list, (int)((row << 16) | ROW_LABEL_PART), text);
}
