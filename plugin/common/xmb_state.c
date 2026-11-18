// SPDX-License-Identifier: GPL-3.0-or-later
// paf (4.82/4.88 vsh.self): View::Activate makes a view the active one and gives the focus to its
// active page node (+0x180); PageActivate(node) sets that node and focuses the page's widget.
// Neither focuses the XMB's list inside the page, which explore_plugin does itself; its own
// Menu::open saves and restores the active page node the same way.
#include "xmb_state.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "paf.h"
#include "vtable.h"
#include "xmb_list.h"

// PhWidget::SetFocus's mode, as explore_plugin focuses its lists.
#define FOCUS_MODE 2
#define VIEW_ACTIVE_PAGE 0x180
// The file's words, each as hex and a space: explore_plugin's view, its active page node, its
// focused list.
#define STATE_WORDS 3
#define STATE_BYTES (STATE_WORDS * (OWNS_HEX_DIGITS + 1))

/** What xmb_state_save saved; zero where nothing was. */
typedef struct
{
    uint32_t view;
    uint32_t page;
    uint32_t list;
} xmb_state;

/** Takes the saved state, which is used up; all zero when there is none. */
static xmb_state take_state(void)
{
    xmb_state state = {0, 0, 0};
    char text[STATE_BYTES + 1];
    const int64_t read = owns_read_file(OWNS_XMB_STATE, text, sizeof text);
    cellFsUnlink(OWNS_XMB_STATE);
    if (read < STATE_BYTES - 1)
    {
        return state;
    }
    int valid = 1;
    state.view = owns_text_get_hex(text, &valid);
    state.page = owns_text_get_hex(text + OWNS_HEX_DIGITS + 1, &valid);
    state.list = owns_text_get_hex(text + 2 * (OWNS_HEX_DIGITS + 1), &valid);
    if (!valid)
    {
        state.view = state.page = state.list = 0;
    }
    return state;
}

void xmb_state_save(void)
{
    const int explore = View_Find("explore_plugin");
    if (explore == 0)
    {
        cellFsUnlink(OWNS_XMB_STATE);
        return;
    }
    const uint32_t words[STATE_WORDS] = {
        (uint32_t)explore,
        object_word((const void *)(uintptr_t)explore, VIEW_ACTIVE_PAGE),
        (uint32_t)(uintptr_t)xmb_focused_list(),
    };
    char text[STATE_BYTES + 1];
    size_t length = 0;
    for (int i = 0; i < STATE_WORDS; ++i)
    {
        length = owns_text_put_hex(text, length, words[i]);
        text[length++] = ' ';
    }
    text[length - 1] = '\0';
    owns_log(text);
    if (!owns_write_file(OWNS_XMB_STATE, text))
    {
        owns_log("XMB state not saved");
        cellFsUnlink(OWNS_XMB_STATE);
    }
}

/** The saved list, if it is still one of explore_plugin's lists; a gone list is not touched. */
static void *current_list(uint32_t saved)
{
    void *lists[XMB_LISTS_MAX];
    const int count = xmb_lists(lists, XMB_LISTS_MAX);
    for (int i = 0; i < count; ++i)
    {
        if ((uint32_t)(uintptr_t)lists[i] == saved)
        {
            return lists[i];
        }
    }
    owns_log_value("saved list gone", saved);
    return NULL;
}

void xmb_state_restore(void)
{
    const xmb_state state = take_state();
    const int explore = View_Find("explore_plugin");
    if (explore != 0)
    {
        View_Activate(explore);
        // The page node is only trusted from the same explore_plugin it was saved from.
        if (state.page != 0 && state.view == (uint32_t)explore)
        {
            View_PageActivatePage(explore, (void *)(uintptr_t)state.page);
        }
        else
        {
            View_PageActivateByName(explore, XMB_LIST_PAGE);
        }
        void *list = state.list != 0 ? current_list(state.list) : xmb_focused_list();
        if (list != NULL)
        {
            PhWidget_SetFocus(list, NULL, FOCUS_MODE);
        }
        owns_log_value("explore list focused", (uint32_t)(uintptr_t)list);
        return;
    }
    const int xmb = View_Find("xmb_plugin");
    if (xmb != 0)
    {
        View_Activate(xmb);
        View_PageActivateByName(xmb, "page_xmb");
    }
}
