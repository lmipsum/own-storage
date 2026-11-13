// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveScanner.h"

#include "app/DebugLog.h"
#include "sync/CloudStorage.h"
#include "sync/SyncState.h"

#include <string>

namespace
{
// Every user profile is a directory here, with its saves in <profile>/savedata.
const char kProfilesDirectory[] = "/dev_hdd0/home";
const char kSavesDirectory[] = "/savedata";
// The PS1 and PS2 memory cards, one file each.
const char kMemoryCardsDirectory[] = "/dev_hdd0/savedata/vmc";

/**
 * An entry that is still to be uploaded; Drive's id and details are added once it is there.
 * @param title its name
 * @param path where it is on the console
 * @param mimeType folderMime for a directory, octetMime for a file
 * @param parentFolder the title of the folder that holds it on Drive
 */
Json NewEntry(const std::string &title, const std::string &path, const std::string &mimeType,
              const std::string &parentFolder)
{
    Json entry;
    entry.Add("title", Json(title));
    entry.Add("path", Json(path));
    entry.Add("mimeType", Json(mimeType));
    entry.Add("parentFolder", Json(parentFolder));
    entry.Add("status", Json(uploadStatus));
    return entry;
}
} // namespace

SaveScanner::SaveScanner(const LocalStorage &storage) : m_storage(storage) {}

Json::Array SaveScanner::Scan() const
{
    Json::Array profiles;
    Json::Array saveFolders;
    Json::Array files;
    for (const std::string &profile : m_storage.List(kProfilesDirectory))
    {
        const std::string savesPath =
            std::string(kProfilesDirectory) + "/" + profile + kSavesDirectory;
        profiles.push_back(NewEntry(profile, savesPath, folderMime, driveFolderName));
        for (const std::string &save : m_storage.List(savesPath))
        {
            const std::string savePath = savesPath + "/" + save;
            saveFolders.push_back(NewEntry(save, savePath, folderMime, profile));
            for (const std::string &file : m_storage.List(savePath))
            {
                files.push_back(NewEntry(file, savePath + "/" + file, octetMime, save));
            }
        }
    }

    Json::Array entries = profiles;
    entries.insert(entries.end(), saveFolders.begin(), saveFolders.end());
    entries.insert(entries.end(), files.begin(), files.end());
    for (const std::string &card : m_storage.List(kMemoryCardsDirectory))
    {
        entries.push_back(NewEntry(card, std::string(kMemoryCardsDirectory) + "/" + card, octetMime,
                                   driveFolderName));
    }
    debugPrintf("SaveScanner::Scan: %u profiles, %u save folders, %u files\n",
                static_cast<unsigned>(profiles.size()), static_cast<unsigned>(saveFolders.size()),
                static_cast<unsigned>(files.size()));
    return entries;
}
