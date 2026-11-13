// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/SaveScanner.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

/**
 * Keeps a user's saves and their backup in line. Every save is one archive in the cloud. A save
 * that changed since its base only on the console goes up, one that changed only in the cloud
 * comes down. A save that changed on both sides, or was deleted on the console, is left as it
 * is: that needs the user's say. For the sync thread only.
 */
class Sync
{
  public:
    /**
     * @param cloud where the backup is kept
     * @param state the base of every save
     * @param storage the console's hard disk
     * @param display what the user sees of the sync
     * @param rootFolderId the cloud id of the Own Storage folder
     * @param savedata the user's savedata directory
     * @param ownSave a save folder of the app itself, which is never backed up
     * @param workDirectory where archives are kept while they move
     */
    Sync(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
         const std::string &rootFolderId, const std::string &savedata, const std::string &ownSave,
         const std::string &workDirectory);
    Sync(const Sync &) = delete;
    Sync &operator=(const Sync &) = delete;

    /**
     * Brings every save and its backup in line, as far as that needs no one's say.
     * @return false when the cloud's saves could not be listed, so nothing was done
     */
    bool Run();

  private:
    /** A save's archive in the cloud. */
    struct CloudSave
    {
        std::string fileId;
        std::string md5;
    };

    /** How far the upper progress bar is. */
    class Progress
    {
      public:
        Progress(SyncDisplay &display, std::size_t total);
        /** Counts one more save done and moves the bar. */
        void Advance();

      private:
        SyncDisplay &m_display;
        std::size_t m_total;
        std::size_t m_done;
        unsigned int m_shownPercent;
    };

    /**
     * The save archives in the cloud, by save name. Of two archives of one save the one the base
     * names is taken, or else the one with the lower id, so every run takes the same. Two consoles
     * that each made an archive of a save before either saw the other's thus keep to their own,
     * rather than one silently overwriting the other's progress.
     * @param saves receives them
     * @return false when they cannot be listed
     */
    bool ListCloudSaves(std::map<std::string, CloudSave> &saves);
    /**
     * Writes a save's archive.
     * @param save the save
     * @param path where the archive goes
     * @return the archive's MD5; empty when a file of the save cannot be read or archived, or
     * the archive cannot be written
     */
    std::string WriteArchive(const LocalSave &save, const std::string &path);
    /** Where a save's archive is kept while it moves. */
    std::string ArchivePath(const std::string &name) const;
    /**
     * Queues a save's archive to go up.
     * @param save the save
     * @param fileId the archive to replace in the cloud; empty to create one
     * @param progress the bar to move when it is up
     */
    void QueueUpload(const LocalSave &save, const std::string &fileId, Progress &progress);
    /**
     * Brings a save down from the cloud. The archive is checked whole before any file of the save
     * is replaced, and the base says a download is under way until every file is written.
     * @param name the save
     * @param cloudSave its archive in the cloud
     * @return false when it could not be brought down
     */
    bool Download(const std::string &name, const CloudSave &cloudSave);
    /** True when the user stopped the sync or the cloud has no room left. */
    bool Stopped() const;

    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    const SaveScanner m_scanner;
    const std::string m_rootFolderId;
    const std::string m_savedata;
    const std::string m_ownSave;
    const std::string m_workDirectory;
};
