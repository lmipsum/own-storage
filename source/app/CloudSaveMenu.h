// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SaveBrowser.h"
#include "ui/Menu.h"
#include "ui/SyncDialog.h"

/**
 * Lists the games with saves in the cloud, then a game's saves, and loads the save the user
 * picks, until the user goes back. App thread only.
 * @param menu where the lists are shown
 * @param dialog shows a download, and asks before a save is replaced
 * @param browser the saves in the cloud
 * @return false when the saves in the cloud cannot be listed
 */
bool BrowseCloudSaves(Menu &menu, SyncDialog &dialog, SaveBrowser &browser);
