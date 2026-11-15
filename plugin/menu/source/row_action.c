// SPDX-License-Identifier: GPL-3.0-or-later
#include "row_action.h"

#include <cell/fs/cell_fs_file_api.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"

// Room for a save folder's name, and for an action with it.
#define SAVE_NAME_BYTES 64
#define ACTION_BYTES 80

// Each row's action, before the save folder's name; the app takes the action file's text as it
// is, so it ends with no line end.
static const char *const kRowActions[OWNS_MENU_ROWS] = {
    [OWNS_ROW_INFO] = "info:",
    [OWNS_ROW_DELETE] = "delete:",
};

int row_action_write(uint32_t row)
{
    char name[SAVE_NAME_BYTES];
    if (row >= OWNS_MENU_ROWS || owns_read_file(OWNS_MENU_TARGET, name, sizeof name) <= 0)
    {
        owns_log_value("no action for row", row);
        return 0;
    }
    char action[ACTION_BYTES];
    const size_t length = owns_text_append(action, 0, sizeof action, kRowActions[row]);
    owns_text_append(action, length, sizeof action, name);
    // A row's script deletes an older action before it writes its own; the file is replaced so.
    cellFsUnlink(OWNS_ACTION_FILE);
    if (!owns_write_file(OWNS_ACTION_FILE, action))
    {
        owns_log("action not written");
        cellFsUnlink(OWNS_ACTION_FILE);
        return 0;
    }
    owns_log(action);
    return 1;
}
