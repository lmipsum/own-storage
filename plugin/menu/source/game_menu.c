// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_menu.h"

#include <stddef.h>
#include <stdint.h>

#include "app_action.h"
#include "confirm_page.h"
#include "info_page.h"
#include "menu_text.h"
#include "utf8.h"
#include "widget_text.h"

// explore_plugin's Information rows for a folder: its name and how many saves it has, as
// "%1 Save" or "%1 Saves", %1 the number.
static const menu_label kFolderLabel = {"msg_folder", 1};
static const menu_label kCountLabel = {"msg_savedata_count", 1};
static const menu_label kOneSave = {"msg_savedata_count_value", 1};
static const menu_label kSaves = {"msg_savedatas_count_value", 1};
// The save menu's own question; the system has none for a game's saves in the cloud.
static const menu_label kDeleteAllQuestion = {"msg_owns_delete_all_ask", 0};

#define FOLDER_ROWS 2
// The most digits of a count taken.
#define COUNT_DIGITS_MAX 9

/** Whether a field is a count: digits only, not too many. */
static int is_count(const char *text)
{
    size_t length = 0;
    for (; text[length] != '\0'; ++length)
    {
        if (text[length] < '0' || text[length] > '9' || length >= COUNT_DIGITS_MAX)
        {
            return 0;
        }
    }
    return length > 0;
}

/**
 * The folder's count of saves, as explore_plugin words it: its text for one save or for more,
 * "%1" replaced with the count.
 * @param units receives it, NUL-ended; WIDGET_TEXT_UNITS
 */
static void count_text(const char *count, unsigned short *units)
{
    const int one = count[0] == '1' && count[1] == '\0';
    const unsigned short *format = menu_text(one ? &kOneSave : &kSaves);
    size_t length = 0;
    for (size_t i = 0; format != NULL && format[i] != 0; ++i)
    {
        if (format[i] == '%' && format[i + 1] == '1')
        {
            for (size_t d = 0; count[d] != '\0'; ++d)
            {
                length = utf16_put(units, length, WIDGET_TEXT_UNITS, (unsigned char)count[d]);
            }
            ++i;
        }
        else
        {
            length = utf16_put(units, length, WIDGET_TEXT_UNITS, format[i]);
        }
    }
    units[length] = 0;
}

/** The delete question's answer: Yes has the app delete the game's saves in the cloud. */
static void on_delete_all_answer(int result, void *data)
{
    confirm_page_finish((int)(uintptr_t)data, result, APP_ACTION_DELETE_ALL, ROW_FIELD_GAME_ID);
}

enum menu_end game_menu_run(int view, enum menu_action action, const menu_target *target)
{
    if (action == MENU_ACTION_DOWNLOAD_ALL)
    {
        return app_action_write(APP_ACTION_DOWNLOAD_ALL,
                                menu_target_field(target, ROW_FIELD_GAME_ID))
                   ? MENU_END_START_APP
                   : MENU_END_CLOSE;
    }
    const char *count = menu_target_field(target, ROW_FIELD_COUNT);
    unsigned short count_units[WIDGET_TEXT_UNITS];
    count_text(is_count(count) ? count : "", count_units);
    const info_row rows[FOLDER_ROWS] = {
        {kFolderLabel, menu_target_field(target, ROW_FIELD_TITLE), NULL},
        {kCountLabel, NULL, is_count(count) ? count_units : NULL},
    };
    int opened = 0;
    if (action == MENU_ACTION_INFO)
    {
        opened = info_page_open(view, rows, FOLDER_ROWS, XMB_BACKDROP_INFO);
    }
    else if (action == MENU_ACTION_DELETE_ALL)
    {
        opened = confirm_page_open(view, rows, FOLDER_ROWS, menu_text(&kDeleteAllQuestion),
                                   on_delete_all_answer);
    }
    return opened ? MENU_END_PAGE_OPEN : MENU_END_CLOSE;
}
