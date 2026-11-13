// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SyncState.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "app/Paths.h"

#include <cstdio>
#include <fstream>

const std::string remoteResourceFile = APP_DIR "remote.json";
const std::string remoteBackup = APP_DIR "remote.backup";
// The remote.json entries changed since its last full save, one JSON object per line.
const std::string remoteJournal = APP_DIR "remote.journal";
// remote.json status of an entry whose creation on Drive started but was not recorded.
const std::string creatingStatus = "creating";
Json remoteResourceRoot;
// A full remote.json save costs about 0.2 s on the console, so changes go to the journal and
// remote.json is saved in full only every this many of them. Sync thread only.
const u32 changesPerFullSave = 50;
u32 changesSinceFullSave = 0;

/**
 * Whether an entry still has to be created on Drive, including one whose creation an earlier
 * run started but did not record.
 * @param resource the remote.json entry
 * @return true when its status is "upload" or creatingStatus
 */
bool isPendingCreation(const Json &resource)
{
    const std::string status = resource["status"].Str();
    return status == "upload" || status == creatingStatus;
}

/**
 * Saves remote.json in full, with its backup, and empties the journal it now contains.
 */
void writeChangesToFile()
{
    // Backup remote.json before a write to save from any catastrophes
    const bool backupSaved = SaveStateFile(remoteResourceRoot, remoteBackup);
    const bool saved = SaveStateFile(remoteResourceRoot, remoteResourceFile);

    if (backupSaved && saved)
    {
        std::remove(remoteJournal.c_str());
        changesSinceFullSave = 0;
        debugPrintf("writeChangesToFile: Changes to remote.json written successfully\n");
    }
}

/**
 * Saves one changed remote.json entry: appends it to the journal, and saves remote.json in
 * full every changesPerFullSave changes.
 * @param index the entry's position in remote.json
 */
void recordEntry(u32 index)
{
    const std::string line = remoteResourceRoot["data"][index].Str() + "\n";
    FILE *journal = std::fopen(remoteJournal.c_str(), "ab");
    const bool appended =
        journal != NULL && std::fwrite(line.data(), 1, line.size(), journal) == line.size();
    if (journal != NULL && std::fclose(journal) != 0)
    {
        debugPrintf("recordEntry: could not close %s\n", remoteJournal.c_str());
    }
    if (!appended || ++changesSinceFullSave >= changesPerFullSave)
    {
        writeChangesToFile();
    }
}

/**
 * Applies the journal an interrupted run left to remote.json's content in memory.
 */
void replayJournal()
{
    // The fields an upload changes; a journaled entry replaces them in the loaded one.
    static const char *const recordedFields[] = {"status",       "id",       "parentid",
                                                 "modifiedDate", "fileSize", "md5Checksum"};
    std::ifstream journal(remoteJournal.c_str(), std::ios::binary);
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
        const Json::Array entries = remoteResourceRoot["data"].AsArray();
        u32 i = 0;
        while (i < entries.size() && entries[i]["path"].Str() != change["path"].Str())
        {
            ++i;
        }
        if (i == entries.size())
        {
            remoteResourceRoot["data"].Append(change);
        }
        else
        {
            for (const char *field : recordedFields)
            {
                if (change.Has(field))
                {
                    remoteResourceRoot["data"][i].Add(field, change[field]);
                }
            }
        }
        ++replayed;
    }
    debugPrintf("replayJournal: %u changes of an interrupted run applied\n", replayed);
}

/**
 * Saves that an entry's creation on Drive is about to start. A run that ends before it records
 * the answer leaves the entry in creatingStatus, so the next run looks for the item before
 * creating it again.
 * @param index the entry's position in remote.json
 */
void markCreating(u32 index)
{
    remoteResourceRoot["data"][index].Add("status", Json(creatingStatus));
    recordEntry(index);
}

/**
 * Saves that a folder exists on Drive.
 * @param index the folder's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param folder what Drive says about the folder
 */
void recordFolder(u32 index, const std::string &parentId, const Json &folder)
{
    remoteResourceRoot["data"][index].Add("id", Json(folder["id"].Str()));
    remoteResourceRoot["data"][index].Add("parentid", Json(parentId));
    remoteResourceRoot["data"][index].Add("modifiedDate", Json(folder["modifiedDate"].Str()));
    remoteResourceRoot["data"][index].Add("status", Json("synced"));
    recordEntry(index);
}

/**
 * Saves that a file is stored on Drive.
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param file what Drive says about the file
 */
void recordFile(u32 index, const std::string &parentId, const Json &file)
{
    remoteResourceRoot["data"][index].Add("id", Json(file["id"].Str()));
    remoteResourceRoot["data"][index].Add("parentid", Json(parentId));
    remoteResourceRoot["data"][index].Add("modifiedDate", Json(file["modifiedDate"].Str()));
    remoteResourceRoot["data"][index].Add("fileSize", Json(file["fileSize"].Str()));
    remoteResourceRoot["data"][index].Add("md5Checksum", Json(file["md5Checksum"].Str()));
    remoteResourceRoot["data"][index].Add("status", Json("synced"));
    recordEntry(index);
}
