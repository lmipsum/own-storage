// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"

#include <ppu-types.h>

#include <optional>
#include <string>

/** The folder in the user's Drive that holds every backup. */
const std::string driveFolderName = "Own Storage";

/** remote.json status of an entry that is still to be created on Drive. */
const std::string uploadStatus = "upload";
/** remote.json status of an entry whose creation on Drive started but was not recorded. */
const std::string creatingStatus = "creating";
/** remote.json status of an entry that Drive holds as the console does. */
const std::string syncedStatus = "synced";
/** remote.json status of a file that changed on the console after it went to Drive. */
const std::string updateStatus = "update";

/**
 * remote.json: every folder and file of the backup, with its Drive id and status. What Drive
 * answers is appended to a journal at once and remote.json is saved in full only now and then,
 * so an interrupted run loses no Drive id it was given. For the sync thread only.
 */
class SyncState
{
  public:
    /**
     * @param file remote.json
     * @param backup the copy every full save writes next to it
     * @param journal the changes since the last full save, one entry per line
     */
    SyncState(const std::string &file, const std::string &backup, const std::string &journal);
    SyncState(const SyncState &) = delete;
    SyncState &operator=(const SyncState &) = delete;

    /**
     * Reads remote.json, or its backup, and applies the journal an interrupted run left.
     * @return false when neither file holds a state; there are then no entries
     */
    bool Load();
    /**
     * Takes over another state, such as the one Drive holds, and saves it in full. The state
     * is shared, not copied, so the caller should not change it afterwards.
     * @param state the state; one without a data array gets an empty one
     */
    void Reset(const Json &state);
    /**
     * Starts over with the given entries, such as the first scan of the console, and saves them.
     * The entries are shared, not copied, so the caller should not change them afterwards.
     * @param entries the entries
     */
    void Reset(const Json::Array &entries);
    /** Saves remote.json in full, with its backup, and empties the journal it now contains. */
    void SaveAll();

    /** How many entries there are. */
    u32 Size() const;
    /** The entry at index; it shares the stored one, so change entries only through here. */
    Json Entry(u32 index) const;
    /** Adds an entry found on the console; it is saved with the next full save. */
    void Append(const Json &entry);
    /** Marks a file that changed on the console; it is saved with the next full save. */
    void MarkChanged(u32 index);
    /**
     * Saves that an entry's creation on Drive is about to start. A run that ends before it
     * records the answer leaves the entry in creatingStatus, so the next run looks for the item
     * before creating it again.
     * @param index the entry
     */
    void MarkCreating(u32 index);
    /**
     * Saves that a folder exists on Drive.
     * @param index the folder's entry
     * @param parentId the Drive folder that holds it
     * @param folder what Drive says about the folder
     */
    void RecordFolder(u32 index, const std::string &parentId, const Json &folder);
    /**
     * Saves that a file is stored on Drive.
     * @param index the file's entry
     * @param parentId the Drive folder that holds it
     * @param file what Drive says about the file
     */
    void RecordFile(u32 index, const std::string &parentId, const Json &file);

    /**
     * Finds the Drive id of the folder that holds an entry: the Own Storage folder, or the
     * folder entry of the directory the entry is in. A title alone is not enough, as two
     * profiles can hold a save of the same name.
     * @param entry the entry
     * @param rootFolderId the Drive id of the Own Storage folder
     * @return the folder's id; none while the folder does not exist on Drive yet
     */
    std::optional<std::string> ParentId(const Json &entry, const std::string &rootFolderId) const;

    /**
     * Whether an entry still has to be created on Drive, including one whose creation an
     * earlier run started but did not record.
     */
    static bool IsPendingCreation(const Json &entry);

  private:
    /** Sets an entry's status without saving it. */
    void SetStatus(u32 index, const std::string &status);
    /** Appends an entry to the journal, and saves in full now and then. */
    void Record(u32 index);
    /** Applies the journal to the entries in memory. */
    void ReplayJournal();

    std::string m_file;
    std::string m_backup;
    std::string m_journal;
    Json m_root;
    u32 m_changesSinceFullSave;
};
