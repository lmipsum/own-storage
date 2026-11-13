// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudStorage.h"
#include "sync/SyncState.h"

#include <map>
#include <string>

/** A save's archive in the cloud. */
struct CloudSave
{
    std::string fileId;
    std::string md5;
    /** The game's title, as the console that sent the archive read it; may be empty. */
    std::string title;
    /** The archive's size in bytes. */
    unsigned long long size;
    /** When it was last stored, as Drive gives it: UTC in RFC 3339. */
    std::string modified;
};

/**
 * The title a save's archive has in the cloud.
 * @param save the save folder's name
 * @return the title
 */
std::string ArchiveTitle(const std::string &save);

/**
 * The save archives in a cloud folder, by save name. Of two archives of one save the one the base
 * names is taken, or else the one with the lower id, so every run takes the same. Two consoles
 * that each made an archive of a save before either saw the other's thus keep to their own,
 * rather than one silently overwriting the other's progress.
 * @param cloud the cloud
 * @param folderId the folder that holds the archives
 * @param ownSave a save folder of the app itself, whose archive is left out
 * @param state the bases, which settle two archives of one save
 * @param saves receives the archives
 * @return false when they cannot be listed
 */
bool ListCloudSaves(CloudStorage &cloud, const std::string &folderId, const std::string &ownSave,
                    const SyncState &state, std::map<std::string, CloudSave> &saves);
