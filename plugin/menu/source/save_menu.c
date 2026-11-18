// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_menu.h"

#include <stdint.h>

#include "app_action.h"
#include "confirm_page.h"
#include "info_page.h"
#include "menu_text.h"

/** A row of a save's information page: its label and the record's field it shows. */
typedef struct
{
    menu_label label;
    int field;
} save_row;

// The rows explore_plugin's Information shows for a save; the delete question shows all but the
// last, as the Saved Data Utility's does.
static const save_row kSaveRows[] = {
    {{"msg_title", 1}, ROW_FIELD_TITLE},          {{"msg_subtitle", 1}, ROW_FIELD_SUBTITLE},
    {{"msg_updated_date", 1}, ROW_FIELD_UPDATED}, {{"msg_size", 1}, ROW_FIELD_SIZE},
    {{"msg_info_detail", 1}, ROW_FIELD_DETAIL},
};

#define SAVE_ROWS ((int)(sizeof kSaveRows / sizeof kSaveRows[0]))
#define DELETE_ROWS (SAVE_ROWS - 1)

// The delete question, which the Saved Data Utility (ps3_savedata_plugin) asks; that plugin is not
// loaded on the XMB, so the save menu's RCO has a copy under its label.
static const menu_label kDeleteQuestion = {"msg_savedata_delete_ask", 0};

/** The save's rows with their values from the target. */
static void fill_rows(const menu_target *target, info_row *rows)
{
    for (int i = 0; i < SAVE_ROWS; ++i)
    {
        rows[i].label = kSaveRows[i].label;
        rows[i].value = menu_target_field(target, kSaveRows[i].field);
        rows[i].value_units = NULL;
    }
}

/** The delete question's answer: Yes has the app delete the save. */
static void on_delete_answer(int result, void *data)
{
    confirm_page_finish((int)(uintptr_t)data, result, APP_ACTION_DELETE, ROW_FIELD_SAVE_NAME);
}

enum menu_end save_menu_run(int view, enum menu_action action, const menu_target *target)
{
    info_row rows[SAVE_ROWS];
    fill_rows(target, rows);
    int opened = 0;
    if (action == MENU_ACTION_INFO)
    {
        opened = info_page_open(view, rows, SAVE_ROWS, XMB_BACKDROP_INFO);
    }
    else if (action == MENU_ACTION_DELETE)
    {
        opened = confirm_page_open(view, rows, DELETE_ROWS, menu_text(&kDeleteQuestion),
                                   on_delete_answer);
    }
    return opened ? MENU_END_PAGE_OPEN : MENU_END_CLOSE;
}
