// SPDX-License-Identifier: GPL-3.0-or-later
// The Saved Data Utility's delete question (ps3_savedata_plugin, 4.82/4.88): the save's texts on
// its page_plugin_delete, each set with SetText's part 0 (0x3059c), its looks (0x400, 0xc00)
// behind it (0x1e7c8), and over it a vsh::MessageDialog with its question and Yes and No, opened
// on an empty page of its own. The dialog answers through its callback, 0 for Yes, and goes by
// itself; ps3_savedata_plugin only forgets it then.
#include "confirm_page.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>

#include "app_action.h"
#include "menu_finish.h"
#include "menu_target.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "paf.h"
#include "vsh_types.h"
#include "vshcommon.h"
#include "widget_text.h"
#include "xmb_backdrop.h"

// ps3_savedata_plugin's arguments for its question.
#define DIALOG_YES_NO 0
#define DIALOG_ICON 5
#define DIALOG_BUTTONS 1
#define DIALOG_LAST 0x64
#define DIALOG_YES 0
// The most units of a question; more are left out.
#define QUESTION_UNITS 192
// A text widget's own text.
#define TEXT_PART 0

// The page's text widgets, as owns_menu.json names them after ps3_savedata_plugin's.
static const char *const kTextWidgets[CONFIRM_TEXTS] = {
    [CONFIRM_TITLE] = "text_delete_title",      [CONFIRM_SUBTITLE] = "text_delete_sub_title",
    [CONFIRM_DATE] = "text_delete_create_date", [CONFIRM_SIZE] = "text_delete_data_size",
    [CONFIRM_DETAIL] = "text_delete_detail",
};

/** Opens the delete page with its texts, the Saved Data Utility's looks behind it. */
static int open_page(int view, const confirm_value *values)
{
    xmb_backdrop_set(XMB_BACKDROP_UTILITY);
    View_Activate(view);
    // Only an RCO's first page is built as the view loads; this one must be opened first.
    if (View_PageOpen(view, OWNS_DELETE_PAGE, 0, 0, -1, 0, 0) == NULL)
    {
        xmb_backdrop_clear(XMB_BACKDROP_UTILITY);
        return 0;
    }
    View_PageActivateByName(view, OWNS_DELETE_PAGE);
    for (int i = 0; i < CONFIRM_TEXTS; ++i)
    {
        void *widget = View_FindWidget(view, kTextWidgets[i]);
        if (widget == NULL)
        {
            owns_log(kTextWidgets[i]);
        }
        else if (values[i].units != NULL)
        {
            widget_set_text(widget, TEXT_PART, values[i].units);
        }
        else if (values[i].text != NULL)
        {
            widget_set_utf8(widget, TEXT_PART, values[i].text);
        }
    }
    return 1;
}

/** Keeps the open dialog, as hex, for recovery, which can take it away. */
static void keep_dialog(const void *dialog)
{
    char text[OWNS_HEX_DIGITS + 1];
    text[owns_text_put_hex(text, 0, (uint32_t)(uintptr_t)dialog)] = '\0';
    if (!owns_write_file(OWNS_DIALOG_OPEN, text))
    {
        owns_log("dialog not kept");
    }
}

/** Opens the dialog on its page with the question; NULL when it did not open. */
static void *open_dialog(int view, const unsigned short *question, confirm_answer on_answer)
{
    View_PageOpen(view, OWNS_DIALOG_PAGE, 0, 0, -1, 0, 0);
    View_PageActivateByName(view, OWNS_DIALOG_PAGE);
    void *parent = View_FindWidget(view, OWNS_DIALOG_PAGE);
    if (parent == NULL || question == NULL)
    {
        return NULL;
    }
    // The dialog copies the text, so a long one may point at this buffer as its heap.
    unsigned short units[QUESTION_UNITS];
    uint32_t length = 0;
    for (; question[length] != 0 && length + 1 < QUESTION_UNITS; ++length)
    {
        units[length] = question[length];
    }
    units[length] = 0;
    vsh_wstring text = {0, {{0}}, length, VSH_WSTRING_IN_PLACE};
    if (length <= VSH_WSTRING_IN_PLACE)
    {
        for (uint32_t i = 0; i <= length; ++i)
        {
            text.data.text[i] = units[i];
        }
    }
    else
    {
        text.data.heap = units;
        text.capacity = length;
    }
    return MessageDialog_Open(parent, &text, DIALOG_YES_NO, (void *)on_answer, DIALOG_ICON,
                              DIALOG_BUTTONS, (void *)(uintptr_t)view, DIALOG_LAST);
}

int confirm_page_open(int view, const confirm_value *values, const unsigned short *question,
                      confirm_answer on_answer)
{
    if (!open_page(view, values))
    {
        return 0;
    }
    void *dialog = open_dialog(view, question, on_answer);
    owns_log_value("dialog opened", (uint32_t)(uintptr_t)dialog);
    if (dialog == NULL)
    {
        menu_close_pages(view, MENU_PAGES_QUESTION, MENU_END_CLOSE);
        return 1;
    }
    keep_dialog(dialog);
    return 1;
}

void confirm_page_finish(int view, int result, const char *action, int field)
{
    // The dialog is gone by itself; recovery must not take it away again.
    cellFsUnlink(OWNS_DIALOG_OPEN);
    owns_log_value("answer", (uint32_t)result);
    enum menu_end end = MENU_END_CLOSE;
    menu_target target;
    if (result == DIALOG_YES && menu_target_read(&target))
    {
        if (app_action_write(action, menu_target_field(&target, field)))
        {
            end = MENU_END_START_APP;
        }
        menu_target_free(&target);
    }
    menu_close_pages(view, MENU_PAGES_QUESTION, end);
}
