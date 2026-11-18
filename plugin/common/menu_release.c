// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_release.h"

#include <cell/fs/cell_fs_file_api.h>

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"

// The request's text: what follows the unload.
#define RELEASE_ONLY "release"
#define RELEASE_SWITCH_LIST "switch"
#define REQUEST_BYTES 16

void menu_release_request(enum menu_release_then then)
{
    if (!owns_write_file(OWNS_MENU_RELEASE,
                         then == MENU_RELEASE_SWITCH_LIST ? RELEASE_SWITCH_LIST : RELEASE_ONLY))
    {
        owns_log("release not requested");
    }
}

int menu_release_take(enum menu_release_then *then)
{
    char request[REQUEST_BYTES];
    const int64_t read = owns_read_file(OWNS_MENU_RELEASE, request, sizeof request);
    if (read < 0)
    {
        return 0;
    }
    cellFsUnlink(OWNS_MENU_RELEASE);
    static const char kSwitch[] = RELEASE_SWITCH_LIST;
    int same = read == (int64_t)(sizeof kSwitch - 1);
    for (int64_t i = 0; same && i < read; ++i)
    {
        same = request[i] == kSwitch[i];
    }
    *then = same ? MENU_RELEASE_SWITCH_LIST : MENU_RELEASE_ONLY;
    return 1;
}
