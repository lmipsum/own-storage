// SPDX-License-Identifier: GPL-3.0-or-later
// The Saved Data Utility's delete question (ps3_savedata_plugin, 4.82/4.88): the save's rows on a
// full-screen page, and over them a vsh::MessageDialog with its question and Yes and No, opened
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

// ps3_savedata_plugin's arguments for its question.
#define DIALOG_YES_NO 0
#define DIALOG_ICON 5
#define DIALOG_BUTTONS 1
#define DIALOG_LAST 0x64
#define DIALOG_YES 0
// The most units of a question; more are left out.
#define QUESTION_UNITS 192

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

int confirm_page_open(int view, const info_row *rows, int count, const unsigned short *question,
                      confirm_answer on_answer)
{
    if (!info_page_open(view, rows, count, XMB_BACKDROP_UTILITY))
    {
        return 0;
    }
    void *dialog = open_dialog(view, question, on_answer);
    owns_log_value("dialog opened", (uint32_t)(uintptr_t)dialog);
    if (dialog == NULL)
    {
        menu_close_pages(view, 1, MENU_END_CLOSE);
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
    menu_close_pages(view, 1, end);
}
