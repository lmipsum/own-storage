// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveScanner.h"

#include "app/DebugLog.h"
#include "sync/CloudStorage.h"
#include "sync/SyncState.h"

#include <string>

namespace
{
/**
 * An entry that is still to be uploaded; Drive's id and details are added once it is there.
 * @param title its name
 * @param path where it is in the savedata directory
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

std::string SavedataDirectory(const std::string &user)
{
    return usersDirectory + "/" + user + "/savedata";
}

SaveScanner::SaveScanner(const LocalStorage &storage, const std::string &savedata,
                         const std::string &ownSave)
    : m_storage(storage), m_savedata(savedata), m_ownSave(ownSave)
{
}

Json::Array SaveScanner::Scan() const
{
    Json::Array entries;
    Json::Array files;
    for (const std::string &save : m_storage.List(m_savedata))
    {
        if (save == m_ownSave)
        {
            continue;
        }
        entries.push_back(NewEntry(save, save, folderMime, driveFolderName));
        for (const std::string &file : m_storage.List(m_savedata + "/" + save))
        {
            files.push_back(NewEntry(file, save + "/" + file, octetMime, save));
        }
    }
    debugPrintf("SaveScanner::Scan: %u save folders, %u files\n",
                static_cast<unsigned>(entries.size()), static_cast<unsigned>(files.size()));
    entries.insert(entries.end(), files.begin(), files.end());
    return entries;
}
