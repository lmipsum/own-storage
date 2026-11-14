// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/CloudSaveText.h"
#include "sync/SaveBrowser.h"
#include "ui/Menu.h"
#include "ui/SyncDialog.h"

#include <functional>
#include <string>

/**
 * Lists the games with saves in the cloud, then a game's saves, and loads or deletes the save
 * the user picks, until the user goes back. The games list also starts an import of saves from a
 * USB drive. App thread only.
 * @param menu where the lists are shown
 * @param dialog shows a download, and asks before a save is replaced or deleted
 * @param browser the saves in the cloud
 * @param dates how the console shows dates
 * @param importSaves imports saves into the cloud; returns what came of it, as the user reads it
 * @return false when the saves in the cloud cannot be listed
 */
bool BrowseCloudSaves(Menu &menu, SyncDialog &dialog, SaveBrowser &browser, const DateStyle &dates,
                      const std::function<std::string()> &importSaves);
