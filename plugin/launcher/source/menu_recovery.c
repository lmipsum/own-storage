// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery runs on the launcher's thread, while paf's job may still have work queued for the
// view: taking the dialog away may call its answer handler, which is code of the view's module.
// So the view is not unloaded here; its release is requested, which the launcher takes on its
// next round, after the closes asked for here.
#include "menu_recovery.h"

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

/** Takes away the dialog the view left open, as hex in OWNS_DIALOG_OPEN. */
static void hide_dialog(void)
{
    char text[OWNS_HEX_DIGITS + 1];
    int valid = owns_read_file(OWNS_DIALOG_OPEN, text, sizeof text) == OWNS_HEX_DIGITS;
    cellFsUnlink(OWNS_DIALOG_OPEN);
    const uint32_t dialog = valid ? owns_text_get_hex(text, &valid) : 0;
    if (valid && dialog != 0)
    {
        MessageDialog_HideDelete((void *)(uintptr_t)dialog);
        owns_log_value("dialog taken away", dialog);
    }
}

void menu_recover(void)
{
    hide_dialog();
    const int view = View_Find(OWNS_MENU_VIEW);
    if (view != 0)
    {
        option_menu_close(view);
        View_PageClose(view, OWNS_DIALOG_PAGE);
        View_PageClose(view, OWNS_INFO_PAGE);
    }
    ShowButtonNavigation_Back(0);
    xmb_backdrop_set(XMB_BACKDROP_OFF);
    xmb_state_restore();
    cellFsUnlink(OWNS_MENU_TARGET);
    if (view != 0)
    {
        menu_release_request(MENU_RELEASE_ONLY);
    }
}
