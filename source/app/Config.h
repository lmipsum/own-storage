// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/** What the app keeps in config.json between runs: the sign-in and where the backup is. */
struct Config
{
    /** The Google refresh token; empty when nobody has signed in. */
    std::string refreshToken;
    /** The Drive id of the Own Storage folder; "root" until it is known. */
    std::string rootFolderId = "root";
};

/**
 * Reads config.json, or its backup when config.json is missing or damaged.
 * @param config receives the saved values; untouched when nothing can be read
 * @return false when neither file can be read
 */
bool LoadConfig(Config &config);

/**
 * Saves config.json and its backup.
 * @param config the values to keep
 */
void SaveConfig(const Config &config);
