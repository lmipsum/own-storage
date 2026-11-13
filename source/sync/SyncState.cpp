// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SyncState.h"

#include "app/Files.h"
#include "json/Json.h"

namespace
{
// The key of the base array in the state file, and of each base's fields.
const char kSaves[] = "saves";
const char kName[] = "name";
const char kFileId[] = "id";
const char kMd5[] = "md5";
const char kFingerprint[] = "fingerprint";
const char kDownloading[] = "downloading";
} // namespace

SyncState::SyncState(const std::string &file, const std::string &backup)
    : m_file(file), m_backup(backup)
{
}

bool SyncState::Load()
{
    m_bases.clear();
    const Json state = LoadWithBackup(m_file, m_backup);
    if (!state.Has(kSaves))
    {
        return false;
    }
    for (const Json &save : state[kSaves].AsArray())
    {
        m_bases[save[kName].Str()] = SaveBase{save[kFileId].Str(), save[kMd5].Str(),
                                              save[kFingerprint].Str(), save.Has(kDownloading)};
    }
    return true;
}

std::vector<std::string> SyncState::Names() const
{
    std::vector<std::string> names;
    for (const auto &base : m_bases)
    {
        names.push_back(base.first);
    }
    return names;
}

const SaveBase *SyncState::Find(const std::string &name) const
{
    const auto base = m_bases.find(name);
    return base == m_bases.end() ? nullptr : &base->second;
}

void SyncState::Record(const std::string &name, const SaveBase &base)
{
    m_bases[name] = base;
    Save();
}

void SyncState::Forget(const std::string &name)
{
    m_bases.erase(name);
    Save();
}

void SyncState::Save() const
{
    Json::Array saves;
    for (const auto &base : m_bases)
    {
        Json save;
        save.Add(kName, Json(base.first));
        save.Add(kFileId, Json(base.second.fileId));
        save.Add(kMd5, Json(base.second.md5));
        save.Add(kFingerprint, Json(base.second.fingerprint));
        if (base.second.downloading)
        {
            save.Add(kDownloading, Json("yes"));
        }
        saves.push_back(save);
    }
    Json state;
    state.Add(kSaves, Json(saves));
    // One after the other, so a power cut leaves at most one of the two half written.
    SaveStateFile(state, m_backup);
    SaveStateFile(state, m_file);
}
