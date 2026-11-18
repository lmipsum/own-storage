// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_action.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"

// Room for the longest action with a save folder's name or a game's ID.
#define ACTION_BYTES 80
#define ARGUMENT_MAX_BYTES 32

/**
 * Whether an argument is a save folder's name or a game's ID as the console gives them; another
 * text could end the action's line and add another for the app.
 */
static int argument_valid(const char *argument)
{
    size_t length = 0;
    for (; argument[length] != '\0'; ++length)
    {
        const char c = argument[length];
        const int allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!allowed || length >= ARGUMENT_MAX_BYTES)
        {
            return 0;
        }
    }
    return length > 0;
}

int app_action_write(const char *action, const char *argument)
{
    if (!argument_valid(argument))
    {
        owns_log("no action: argument not valid");
        return 0;
    }
    char text[ACTION_BYTES];
    // The app takes the file's text as it is, so it ends with no line end.
    const size_t length = owns_text_append(text, 0, sizeof text, action);
    owns_text_append(text, length, sizeof text, argument);
    // A row's script deletes an older action before it writes its own; the file is replaced so.
    cellFsUnlink(OWNS_ACTION_FILE);
    if (!owns_write_file(OWNS_ACTION_FILE, text))
    {
        owns_log("action not written");
        cellFsUnlink(OWNS_ACTION_FILE);
        return 0;
    }
    owns_log(text);
    return 1;
}
