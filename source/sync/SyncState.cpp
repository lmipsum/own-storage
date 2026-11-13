// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SyncState.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "sync/CloudStorage.h"

#include <cstdio>
#include <fstream>

namespace
{
// The key of the entry array in remote.json.
const char kEntries[] = "data";
// A full remote.json save costs about 0.2 s on the console, so changes go to the journal and
// remote.json is saved in full only every this many of them.
const u32 kChangesPerFullSave = 50;
// The fields an upload changes; a journaled entry replaces them in the loaded one.
const char *const kRecordedFields[] = {"status",       "id",       "parentid",
                                       "modifiedDate", "fileSize", "md5Checksum"};

/** The state with an empty entry array added when it has none. */
Json WithEntries(const Json &state)
{
    Json withEntries = state;
    if (!withEntries.Has(kEntries))
    {
        withEntries.Add(kEntries, Json(Json::Array()));
    }
    return withEntries;
}
} // namespace

SyncState::SyncState(const std::string &file, const std::string &backup, const std::string &journal)
    : m_file(file), m_backup(backup), m_journal(journal), m_root(WithEntries(Json())),
      m_changesSinceFullSave(0)
{
}

bool SyncState::Load()
{
    const Json loaded = LoadWithBackup(m_file, m_backup);
    m_root = WithEntries(loaded);
    if (loaded.IsEmpty())
    {
        return false;
    }
    ReplayJournal();
    return true;
}

void SyncState::Reset(const Json &state)
{
    m_root = WithEntries(state);
    SaveAll();
}

void SyncState::Reset(const Json::Array &entries)
{
    Json state;
    state.Add(kEntries, Json(entries));
    Reset(state);
}

void SyncState::SaveAll()
{
    const bool backupSaved = SaveStateFile(m_root, m_backup);
    const bool saved = SaveStateFile(m_root, m_file);
    if (backupSaved && saved)
    {
        std::remove(m_journal.c_str());
        m_changesSinceFullSave = 0;
        debugPrintf("SyncState::SaveAll: remote.json saved\n");
    }
}

u32 SyncState::Size() const
{
    return m_root[kEntries].AsArray().size();
}

Json SyncState::Entry(u32 index) const
{
    return m_root[kEntries][index];
}

void SyncState::Append(const Json &entry)
{
    m_root[kEntries].Append(entry);
}

void SyncState::MarkChanged(u32 index)
{
    SetStatus(index, updateStatus);
}

void SyncState::MarkCreating(u32 index)
{
    SetStatus(index, creatingStatus);
    Record(index);
}

void SyncState::RecordFolder(u32 index, const std::string &parentId, const Json &folder)
{
    Json entry = m_root[kEntries][index];
    entry.Add("id", Json(folder["id"].Str()));
    entry.Add("parentid", Json(parentId));
    entry.Add("modifiedDate", Json(folder["modifiedDate"].Str()));
    entry.Add("status", Json(syncedStatus));
    Record(index);
}

void SyncState::RecordFile(u32 index, const std::string &parentId, const Json &file)
{
    Json entry = m_root[kEntries][index];
    entry.Add("id", Json(file["id"].Str()));
    entry.Add("parentid", Json(parentId));
    entry.Add("modifiedDate", Json(file["modifiedDate"].Str()));
    entry.Add("fileSize", Json(file["fileSize"].Str()));
    entry.Add("md5Checksum", Json(file["md5Checksum"].Str()));
    entry.Add("status", Json(syncedStatus));
    Record(index);
}

std::optional<std::string> SyncState::ParentId(const Json &entry,
                                               const std::string &rootFolderId) const
{
    if (entry["parentFolder"].Str() == driveFolderName)
    {
        return rootFolderId;
    }
    const std::string path = entry["path"].Str();
    const std::string directory = path.substr(0, path.rfind('/'));
    for (const Json &folder : m_root[kEntries].AsArray())
    {
        if (folder["mimeType"].Str() == folderMime && folder["path"].Str() == directory &&
            !IsPendingCreation(folder))
        {
            return folder["id"].Str();
        }
    }
    return std::nullopt;
}

bool SyncState::IsPendingCreation(const Json &entry)
{
    const std::string status = entry["status"].Str();
    return status == uploadStatus || status == creatingStatus;
}

void SyncState::SetStatus(u32 index, const std::string &status)
{
    m_root[kEntries][index].Add("status", Json(status));
}

void SyncState::Record(u32 index)
{
    const std::string line = m_root[kEntries][index].Str() + "\n";
    FILE *journal = std::fopen(m_journal.c_str(), "ab");
    const bool appended =
        journal != NULL && std::fwrite(line.data(), 1, line.size(), journal) == line.size();
    if (journal != NULL && std::fclose(journal) != 0)
    {
        debugPrintf("SyncState::Record: could not close %s\n", m_journal.c_str());
    }
    if (!appended || ++m_changesSinceFullSave >= kChangesPerFullSave)
    {
        SaveAll();
    }
}

void SyncState::ReplayJournal()
{
    std::ifstream journal(m_journal.c_str(), std::ios::binary);
    if (!journal)
    {
        return;
    }
    u32 replayed = 0;
    std::string line;
    while (std::getline(journal, line))
    {
        // A power cut can leave the last line half written; it does not parse and is skipped.
        const Json change = Json::Parse(line);
        if (!change.Has("path"))
        {
            continue;
        }
        const Json::Array entries = m_root[kEntries].AsArray();
        u32 i = 0;
        while (i < entries.size() && entries[i]["path"].Str() != change["path"].Str())
        {
            ++i;
        }
        if (i == entries.size())
        {
            m_root[kEntries].Append(change);
        }
        else
        {
            for (const char *field : kRecordedFields)
            {
                if (change.Has(field))
                {
                    m_root[kEntries][i].Add(field, change[field]);
                }
            }
        }
        ++replayed;
    }
    debugPrintf("SyncState::ReplayJournal: %u changes of an interrupted run applied\n", replayed);
}
