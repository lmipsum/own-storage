// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_finish.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>

#include "menu_release.h"
#include "option_menu_close.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "paf.h"
#include "vshcommon.h"
#include "xmb_backdrop.h"
#include "xmb_state.h"

/** The menu's last steps once the XMB has the input: the target goes, the launcher takes over. */
static void hand_over(enum menu_end end)
{
    cellFsUnlink(OWNS_MENU_TARGET);
    menu_release_request(end == MENU_END_SWITCH_LIST ? MENU_RELEASE_SWITCH_LIST
                                                     : MENU_RELEASE_ONLY);
    // After the release, so that the launcher unloads the view before it starts the app.
    if (end == MENU_END_START_APP)
    {
        owns_log(owns_write_file(OWNS_LAUNCH_REQUEST, "launch") ? "launch requested"
                                                                : "launch not requested");
    }
}

void menu_end_closed(enum menu_end end)
{
    if (end == MENU_END_PAGE_OPEN)
    {
        return;
    }
    xmb_state_restore();
    hand_over(end);
}

void menu_close_menu(int view, enum menu_end end)
{
    owns_log_value("option menu closed", (uint32_t)option_menu_close(view));
    menu_end_closed(end);
}

void menu_close_pages(int view, enum menu_pages pages, enum menu_end end)
{
    // Its hint and the XMB's looks go back first, each look with its own close, and the XMB has
    // the input before the pages close, so that neither stays on a page's widget.
    ShowButtonNavigation_Back(0);
    xmb_backdrop_clear(pages == MENU_PAGES_INFO ? XMB_BACKDROP_INFO : XMB_BACKDROP_UTILITY);
    xmb_state_restore();
    if (pages == MENU_PAGES_QUESTION)
    {
        owns_log_value("dialog page closed", (uint32_t)View_PageClose(view, OWNS_DIALOG_PAGE));
        owns_log_value("delete page closed", (uint32_t)View_PageClose(view, OWNS_DELETE_PAGE));
    }
    else
    {
        owns_log_value("info page closed", (uint32_t)View_PageClose(view, OWNS_INFO_PAGE));
    }
    hand_over(end == MENU_END_PAGE_OPEN ? MENU_END_CLOSE : end);
}
