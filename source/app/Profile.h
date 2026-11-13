// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

#include <optional>
#include <string>

/** The save the app keeps in the logged-in user's savedata to find that user; never synced. */
const std::string profileMarkerSave = APP_ID "-PROFILE";

/**
 * Finds the PS3 user who is logged in. The system's save data utility always writes into that
 * user's savedata, so the app saves a marker of this run there and looks for it.
 * @param storage the console's hard disk
 * @return the user's folder name under /dev_hdd0/home, such as "00000001"; none when the
 * marker cannot be saved or found
 */
std::optional<std::string> FindLoggedInUser(const LocalStorage &storage);

/**
 * Makes the directory where the app keeps a user's own files: the sign-in and the sync state.
 * @param storage the console's hard disk
 * @param user the user's folder name under /dev_hdd0/home
 * @return the directory
 */
std::string MakeProfileDirectory(LocalStorage &storage, const std::string &user);
