// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveScanner.h"

#include "app/DebugLog.h"

#include <algorithm>
#include <sstream>

std::string SavedataDirectory(const std::string &user)
{
    return usersDirectory + "/" + user + "/savedata";
}

SaveScanner::SaveScanner(const LocalStorage &storage, const std::string &savedata,
                         const std::string &ownSave)
    : m_storage(storage), m_savedata(savedata), m_ownSave(ownSave)
{
}

std::vector<std::string> SaveScanner::Names() const
{
    std::vector<std::string> names = m_storage.List(m_savedata);
    names.erase(std::remove(names.begin(), names.end(), m_ownSave), names.end());
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<LocalSave> SaveScanner::Scan() const
{
    std::vector<LocalSave> saves;
    for (const std::string &name : Names())
    {
        saves.push_back(ScanSave(name));
    }
    debugPrintf("SaveScanner::Scan: %u save folders\n", static_cast<unsigned int>(saves.size()));
    return saves;
}

LocalSave SaveScanner::ScanSave(const std::string &name) const
{
    LocalSave save;
    save.name = name;
    const std::string folder = m_savedata + "/" + name;
    save.files = m_storage.List(folder);
    std::sort(save.files.begin(), save.files.end());
    std::ostringstream fingerprint;
    for (const std::string &file : save.files)
    {
        FileStatus status = {0, 0};
        m_storage.Status(folder + "/" + file, status);
        fingerprint << file << ' ' << status.size << ' ' << status.modified << '\n';
        save.modified = std::max(save.modified, status.modified);
        save.bytes += status.size;
    }
    save.fingerprint = fingerprint.str();
    return save;
}
