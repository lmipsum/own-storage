// SPDX-License-Identifier: GPL-3.0-or-later
#include "view_choice.h"

#include "owns_log.h"
#include "xmb_view.h"

/** A section's letters, in its submenu's row order; NULL for no section. */
static const char *section_letters(enum menu_section section)
{
    return section == MENU_SECTION_SORT    ? XMB_VIEW_SORT_LETTERS
           : section == MENU_SECTION_GROUP ? XMB_VIEW_GROUP_LETTERS
                                           : NULL;
}

/** The view the row's list shows: its file's, else the one kept for the app. */
static xmb_view shown_view(const menu_head *head)
{
    xmb_view view = xmb_view_read();
    if (head->shown.sort != XMB_VIEW_UNKNOWN)
    {
        view.sort = head->shown.sort;
    }
    if (head->shown.group != XMB_VIEW_UNKNOWN)
    {
        view.group = head->shown.group;
    }
    if (head->place == MENU_PLACE_GAME_FOLDER)
    {
        view.group = XMB_VIEW_BY_TITLE;
    }
    return view;
}

uint32_t view_choice_current(enum menu_section section, const menu_head *head)
{
    const char *letters = section_letters(section);
    const xmb_view view = shown_view(head);
    const char current = section == MENU_SECTION_SORT ? view.sort : view.group;
    for (uint32_t row = 0; letters != NULL && letters[row] != '\0'; ++row)
    {
        if (letters[row] == current)
        {
            return row;
        }
    }
    return 0;
}

int view_choice_take(enum menu_section section, const menu_head *head, uint32_t row)
{
    const char *letters = section_letters(section);
    if (letters == NULL || row >= (uint32_t)menu_choice_count(section))
    {
        return 0;
    }
    xmb_view view = shown_view(head);
    if (section == MENU_SECTION_SORT)
    {
        view.sort = letters[row];
    }
    else
    {
        view.group = letters[row];
    }
    if (!xmb_view_write(view))
    {
        owns_log("view not kept");
        return 0;
    }
    return 1;
}
