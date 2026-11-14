// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudSaves.h"
#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/SaveArchives.h"
#include "sync/SaveProgress.h"
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
 * comes down. A save that changed on both sides, or was deleted on the console, needs the user's
 * say: once the rest is done, the user is asked once per game. For the sync thread only.
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
     * @param target whom a save brought down is made to belong to; null to leave saves as they are
     */
    Sync(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
         const std::string &rootFolderId, const std::string &savedata, const std::string &ownSave,
         const std::string &workDirectory, const SaveTarget *target);
    Sync(const Sync &) = delete;
    Sync &operator=(const Sync &) = delete;

    /**
     * Brings every save and its backup in line, asking the user where that needs a say.
     * @return false when the cloud's saves could not be listed, so nothing was done
     */
    bool Run();
    /** The saves the run brought down as they were, as they could not be made the user's. */
    const std::vector<std::string> &NotResigned() const;

  private:
    /** A save to send up. */
    struct Upload
    {
        const LocalSave *save;
        /** The archive to replace in the cloud; empty to create one. */
        std::string fileId;
    };

    /** A save to bring down. */
    struct Download
    {
        std::string name;
        CloudSave cloud;
    };

    /** Why a save needs the user's say. */
    enum class Doubt
    {
        ChangedOnBothSides,
        DeletedOnConsole,
        /** Deleted here after another console had sent a newer copy, which the user should know. */
        DeletedOnConsoleChangedInCloud
    };

    /** A save that needs the user's say. */
    struct Question
    {
        Doubt doubt;
        std::string name;
        CloudSave cloud;
        /** The save on the console; null when it was deleted there. */
        const LocalSave *local;
    };

    /** What a run does with the saves. */
    struct Plan
    {
        std::vector<Upload> uploads;
        std::vector<Download> downloads;
        std::vector<Question> questions;
    };

    /**
     * Decides what to do with a save, from what it is like on either side and its base. A save
     * that is the same on both sides gets its base at once.
     * @param name the save
     * @param local the save on the console; null when it is not there
     * @param cloud its archive in the cloud; null when there is none
     * @param plan receives the decision
     */
    void Decide(const std::string &name, const LocalSave *local, const CloudSave *cloud,
                Plan &plan);
    /** Sends the plan's uploads, then brings its downloads down, on fresh progress bars. */
    void Transfer(const Plan &plan);
    /**
     * Asks the user once per game what to do with the saves in doubt.
     * @param questions the saves in doubt
     * @return the uploads and downloads the answers call for
     */
    Plan Settle(const std::vector<Question> &questions);
    /**
     * The question for one game.
     * @param doubt why its saves need a say
     * @param titleId the game's ID
     * @param questions its saves
     * @return the question, naming the game and what each answer does
     */
    std::string QuestionText(Doubt doubt, const std::string &titleId,
                             const std::vector<const Question *> &questions) const;
    /**
     * Queues a save's archive to go up.
     * @param save the save
     * @param fileId the archive to replace in the cloud; empty to create one
     * @param progress the bar to move when it is up
     */
    void QueueUpload(const LocalSave &save, const std::string &fileId, SaveProgress &progress);
    /** True when the user stopped the sync or the cloud has no room left. */
    bool Stopped() const;

    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    const SaveScanner m_scanner;
    SaveArchives m_archives;
    const std::string m_rootFolderId;
    const std::string m_ownSave;
    std::vector<std::string> m_notResigned;
};
