// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SaveBrowser.h"
#include "sync/SyncDisplay.h"

#include <string>

/**
 * Does what picking a save in the XMB means: a save the console does not have is offered to be
 * brought down and, when the user does not want it, to be deleted from the cloud; one the console
 * has otherwise is offered to replace it; one the console has already is only said to be there.
 * @param browser the saves in the cloud, listed already
 * @param display asks the user
 * @param name the save folder's name
 * @return what came of it, as the user reads it
 */
std::string PickXmbSave(SaveBrowser &browser, SyncDisplay &display, const std::string &name);
