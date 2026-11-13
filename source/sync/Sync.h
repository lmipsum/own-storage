// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"
#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <cstddef>

#include <string>

/** How reading remote.json from the cloud ended. */
enum class RemoteStateFetch
{
    /** Read, or the cloud has none. */
    Done,
    /** A request failed. */
    Failed,
    /** Downloaded, but it is not a sync state of the savedata directory. */
    Damaged
};

/**
 * Keeps the console's saves and their backup in the cloud the same: what changed on the console
 * goes up, what is missing on the console comes down, and remote.json records where everything
 * is. For the sync thread only.
 */
class Sync
{
  public:
    /**
     * @param cloud where the backup is kept
     * @param state remote.json
     * @param storage the console's hard disk
     * @param display what the user sees of the sync
     * @param rootFolderId the cloud id of the Own Storage folder
     * @param savedata the logged-in user's savedata directory, which entry paths are relative to
     */
    Sync(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
         const std::string &rootFolderId, const std::string &savedata);
    Sync(const Sync &) = delete;
    Sync &operator=(const Sync &) = delete;

    /**
     * Replaces the local state with the remote.json the cloud holds; leaves it as it is when the
     * cloud has none.
     * @return how it ended; unless Done the state in memory is unchanged, and remote.json and the
     * journal are deleted when the download is not a state
     */
    RemoteStateFetch FetchState();
    /**
     * Adds what the console holds but the state does not know, and marks the files that changed
     * on the console since they went up.
     * @param local the entries the save scan found
     */
    void DetectChanges(const Json::Array &local);
    /** Sends what is new or changed, then remote.json. */
    void Upload();
    /** Saves to the console what the backup holds but the console does not. */
    void Download();

  private:
    /** The upper progress bar of an upload: how many of the pending entries are done. */
    class UploadProgress
    {
      public:
        UploadProgress(SyncDisplay &display, std::size_t total);
        /** Sets what is being sent, as the bar names it. */
        void SetLabel(const std::string &label);
        /** Counts one more finished entry and moves the bar. */
        void Advance();

      private:
        SyncDisplay &m_display;
        std::string m_label;
        std::size_t m_total;
        std::size_t m_done;
        unsigned int m_shownPercent;
    };

    /** Where an entry is on the console. */
    std::string LocalPath(const Json &entry) const;
    /** True when the user stopped the sync or the cloud has no room left. */
    bool Stopped() const;
    /**
     * Looks for the item an earlier, interrupted run created for an entry.
     * @param entry the entry
     * @param parentId the folder that holds the item
     * @param item receives the item; without "id" when the entry is not in creatingStatus or the
     * cloud has no such item
     * @return false when the cloud could not be asked, so the entry must not be created now
     */
    bool FindInterruptedCreation(const Json &entry, const std::string &parentId, Json &item);
    /**
     * Settles a file that an interrupted run may already have stored.
     * @param index the file's entry
     * @param parentId the folder that holds it
     * @param progress the bar to move when the stored file is taken over as it is
     * @param fileId receives the stored file to update, or "" when a new one is to be created
     * @return true when nothing is to be sent: the cloud holds the file as it is now, or it could
     * not be asked whether it does
     */
    bool AdoptInterruptedFile(std::size_t index, const std::string &parentId,
                              UploadProgress &progress, std::string &fileId);
    /**
     * Shows on the lower bar what the queue is sending.
     * @param kind what is being sent, in the plural
     * @param title the item starting now
     */
    void ShowRequestsInProgress(const char *kind, const std::string &title);
    /** Fills in a folder to create when its turn comes; false when nothing is to be sent. */
    bool PrepareFolder(std::size_t index, const std::string &parentId, UploadProgress &progress,
                       FolderToCreate &folder);
    /** Saves the folder the cloud created, unless it failed. */
    void FinishFolder(std::size_t index, const std::string &parentId, UploadProgress &progress,
                      const Json &folder);
    /** Fills in a file to store when its turn comes; false when nothing is to be sent. */
    bool PrepareFile(std::size_t index, const std::string &parentId, UploadProgress &progress,
                     FileToStore &file);
    /** Saves the file the cloud stored, unless it failed. */
    void FinishFile(std::size_t index, const std::string &parentId, UploadProgress &progress,
                    const Json &file);
    /**
     * Brings remote.json in the cloud up to the local one, unless it holds the same content
     * already. Comparing the content also catches the changes of a run that ended before it
     * pushed them.
     */
    void PushState();

    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    std::string m_rootFolderId;
    std::string m_savedata;
};
