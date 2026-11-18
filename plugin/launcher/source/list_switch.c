// SPDX-License-Identifier: GPL-3.0-or-later
#include "list_switch.h"

#include <stddef.h>
#include <stdint.h>

#include "explore_query.h"
#include "owns_log.h"

enum list_file list_switch_current(xmb_view *shown)
{
    shown->sort = XMB_VIEW_UNKNOWN;
    shown->group = XMB_VIEW_UNKNOWN;
    explore_url url;
    if (!explore_query_url(&url))
    {
        return LIST_FILE_NONE;
    }
    char text[EXPLORE_URL_MAX];
    explore_url_copy(&url, text);
    owns_log(text);
    size_t at = 0;
    size_t length = 0;
    const enum list_file file = list_file_in_url(text, &at, &length);
    if (file != LIST_FILE_NONE)
    {
        *shown = list_file_view(text + at, length);
    }
    return file;
}

/** Whether explore_plugin's URL still has a text at an offset. */
static int url_has(const explore_url *url, size_t at, const char *text, size_t length)
{
    const volatile char *bytes = (const volatile char *)(uintptr_t)url->address;
    for (size_t i = 0; i < length; ++i)
    {
        if (at + i >= url->length || bytes[at + i] != text[i])
        {
            return 0;
        }
    }
    return 1;
}

int list_switch_to_kept_view(void)
{
    explore_url url;
    if (!explore_query_url(&url))
    {
        owns_log("list not switched: no query");
        return 0;
    }
    char text[EXPLORE_URL_MAX];
    explore_url_copy(&url, text);
    size_t at = 0;
    size_t length = 0;
    const enum list_file file = list_file_in_url(text, &at, &length);
    xmb_view view = xmb_view_read();
    // The saves' file names the list of a game's folder only in a view by title: the folders are
    // that group's, whatever group was kept before.
    if (file == LIST_FILE_SAVES)
    {
        view.group = XMB_VIEW_BY_TITLE;
    }
    char name[LIST_FILE_NAME_BYTES];
    const size_t name_length = list_file_variant(file, view, name);
    if (file == LIST_FILE_NONE || name_length != length)
    {
        owns_log("list not switched: not one of the app's lists");
        return 0;
    }
    if (url_has(&url, at, name, length))
    {
        owns_log("list not switched: it shows the view already");
        return 0;
    }
    // Checked again right before the write, as explore_plugin may have moved on since the copy:
    // at worst another of the app's lists, of a name as long, is switched, never a foreign text.
    if (!url_has(&url, at, text + at, length))
    {
        owns_log("list not switched: its query changed");
        return 0;
    }
    volatile char *bytes = (volatile char *)(uintptr_t)url.address;
    for (size_t i = 0; i < length; ++i)
    {
        bytes[at + i] = name[i];
    }
    owns_log(name);
    // A literal, as explore_plugin's runner may read the command after the call returns.
    return explore_command("reload_list");
}
