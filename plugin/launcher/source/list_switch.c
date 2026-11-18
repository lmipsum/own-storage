// SPDX-License-Identifier: GPL-3.0-or-later
#include "list_switch.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/systime.h>

#include "explore_query.h"
#include "owns_log.h"

/** What switch_url did. */
enum url_switch
{
    URL_REFUSED,
    URL_SAME,
    URL_WRITTEN,
};

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

/**
 * Writes the name of a view's file over the list file a query URL names. Nothing is written
 * unless the URL names one of the app's list files, of the expected kind when one is given, and
 * the view's file name is as long and another.
 * @param expected LIST_FILE_NONE for any of the app's
 */
static enum url_switch switch_url(const explore_url *url, enum list_file expected, xmb_view view)
{
    char text[EXPLORE_URL_MAX];
    explore_url_copy(url, text);
    owns_log(text);
    size_t at = 0;
    size_t length = 0;
    const enum list_file file = list_file_in_url(text, &at, &length);
    char name[LIST_FILE_NAME_BYTES];
    const size_t name_length = list_file_variant(file, view, name);
    if (file == LIST_FILE_NONE || (expected != LIST_FILE_NONE && file != expected) ||
        name_length != length)
    {
        owns_log("list not switched: not one of the app's lists");
        return URL_REFUSED;
    }
    if (url_has(url, at, name, length))
    {
        return URL_SAME;
    }
    // Checked again right before the write, as explore_plugin may have moved on since the copy:
    // at worst another of the app's lists, of a name as long, is switched, never a foreign text.
    if (!url_has(url, at, text + at, length))
    {
        owns_log("list not switched: its query changed");
        return URL_REFUSED;
    }
    volatile char *bytes = (volatile char *)(uintptr_t)url->address;
    for (size_t i = 0; i < length; ++i)
    {
        bytes[at + i] = name[i];
    }
    owns_log(name);
    return URL_WRITTEN;
}

/** Whether a list is the deepest open one. */
static int is_deepest(uint32_t list)
{
    uint32_t lists[EXPLORE_LISTS_MAX];
    const int count = explore_query_lists(lists, EXPLORE_LISTS_MAX);
    return count > 0 && lists[count - 1] == list;
}

/** Waits until a list is the deepest open one again, as long as a command is waited for. */
static int wait_deepest(uint32_t list)
{
    for (int round = 0; round < EXPLORE_WAIT_ROUNDS; ++round)
    {
        if (is_deepest(list))
        {
            return 1;
        }
        sysUsleep(EXPLORE_WAIT_MICROSECONDS);
    }
    owns_log("level not closed");
    return 0;
}

/**
 * From a game's folder to all the saves in one list: the folder's parent, the saves in the cloud,
 * switches to the menu's file of the view, then the folder closes, which a list of all saves has
 * no place for, and the parent reloads.
 */
static int leave_game_folder(const uint32_t *lists, int count, xmb_view view)
{
    explore_url url;
    if (count < 2 || !explore_query_list_url(lists[count - 2], &url))
    {
        owns_log("list not switched: no parent list");
        return 0;
    }
    const enum url_switch parent = switch_url(&url, LIST_FILE_MENU, view);
    if (parent == URL_REFUSED)
    {
        return 0;
    }
    // Checked again right before the close, as the user may have moved on since: close_list closes
    // whatever list is the deepest then.
    const int closed = is_deepest(lists[count - 1]) && explore_command_wait("close_list") &&
                       wait_deepest(lists[count - 2]);
    if (!closed)
    {
        owns_log("game folder not closed");
    }
    // The parent's URL names the new file already, so reload_list is sent even when the close
    // failed: it reloads the parent when that is the deepest list by then, as when the folder
    // closed late or the user went back meanwhile.
    const int reloaded = parent == URL_SAME || explore_command_wait("reload_list");
    return closed && reloaded;
}

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

int list_switch_to_kept_view(void)
{
    uint32_t lists[EXPLORE_LISTS_MAX];
    const int count = explore_query_lists(lists, EXPLORE_LISTS_MAX);
    explore_url url;
    if (count <= 0 || !explore_query_list_url(lists[count - 1], &url))
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
    if (file == LIST_FILE_SAVES && view.group != XMB_VIEW_BY_TITLE)
    {
        return leave_game_folder(lists, count, view);
    }
    // A game's folder stays one of the folders per game, whatever group was kept before.
    if (file == LIST_FILE_SAVES)
    {
        view.group = XMB_VIEW_BY_TITLE;
    }
    // A literal command, as explore_plugin's runner may read it after the call returns.
    return switch_url(&url, LIST_FILE_NONE, view) == URL_WRITTEN &&
           explore_command_wait("reload_list");
}
