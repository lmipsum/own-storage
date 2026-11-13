// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/CloudSaves.h"

#include "app/DebugLog.h"
#include "sync/Tar.h"

#include <stdlib.h>
#include <utility>

namespace
{
// A save's archive in the cloud is named after the save folder.
const char kArchiveSuffix[] = ".tar";
const std::size_t kArchiveSuffixLength = sizeof kArchiveSuffix - 1;

/**
 * The save an archive title names; empty when the title is not a save archive, or names a folder
 * that would not stay in the savedata directory.
 */
std::string SaveName(const std::string &title)
{
    if (title.size() <= kArchiveSuffixLength ||
        title.compare(title.size() - kArchiveSuffixLength, kArchiveSuffixLength, kArchiveSuffix) !=
            0)
    {
        return std::string();
    }
    const std::string name = title.substr(0, title.size() - kArchiveSuffixLength);
    return IsArchivableName(name) ? name : std::string();
}
} // namespace

std::string ArchiveTitle(const std::string &save)
{
    return save + kArchiveSuffix;
}

bool ListCloudSaves(CloudStorage &cloud, const std::string &folderId, const std::string &ownSave,
                    const SyncState &state, std::map<std::string, CloudSave> &saves)
{
    saves.clear();
    Json::Array files;
    if (!cloud.ListFiles(folderId, files))
    {
        return false;
    }
    for (const Json &file : files)
    {
        const std::string name = SaveName(file["title"].Str());
        if (name.empty() || name == ownSave)
        {
            continue;
        }
        const CloudSave save{file["id"].Str(), file["md5Checksum"].Str(), file["description"].Str(),
                             // The console's C++ library offers strtoull only outside std.
                             strtoull(file["fileSize"].Str().c_str(), nullptr, 10),
                             file["modifiedDate"].Str()};
        const auto known = saves.find(name);
        if (known == saves.end())
        {
            saves.insert(std::make_pair(name, save));
            continue;
        }
        debugPrintf("ListCloudSaves: %s is in the cloud twice\n", name.c_str());
        const SaveBase *base = state.Find(name);
        const bool baseNamesThis = base != nullptr && base->fileId == save.fileId;
        const bool baseNamesKnown = base != nullptr && base->fileId == known->second.fileId;
        if (baseNamesThis || (!baseNamesKnown && save.fileId < known->second.fileId))
        {
            known->second = save;
        }
    }
    return true;
}
