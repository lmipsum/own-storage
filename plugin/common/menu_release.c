// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_release.h"

#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"

void menu_release_request(void)
{
    if (!owns_write_file(OWNS_MENU_RELEASE, "release"))
    {
        owns_log("release not requested");
    }
}
