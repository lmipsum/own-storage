// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudSaves.h"
#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/SaveArchives.h"
#include "sync/SaveScanner.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <map>
#include <string>
#include <vector>

/** A game that has saves in the cloud. */
struct CloudGame
{
    std::string titleId;
    /** The game's title as the user sees it; its ID when the title is not known. */
    std::string name;
    /** How the user sees the game named, with its ID. */
    std::string label;
    /** Its saves in the cloud, the one stored last first. */
    std::vector<std::string> saves;
};

/** How a save in the cloud is on the console. */
enum class SaveStatus
{
    /** The console does not have it. */
    Missing,
    /** The console has the cloud's copy. */
    Same,
    /** The console has it with other content. */
    Different
};

/** What loading a save from the cloud came to. */
enum class LoadResult
{
    Loaded,
    /** The console had the cloud's copy already. */
    AlreadyThere,
    /** Loaded as it was, as it could not be made the user's; the game may refuse it. */
    LoadedNotResigned,
    /** The user kept the console's own copy. */
    Kept,
    Failed
};

/** What deleting a save from the cloud came to. */
enum class RemoveResult
{
    /** Moved to the cloud's trash. */
    Removed,
    /** The console has it, so the next sync would only send it up again. */
    OnConsole,
    /** The user kept it. */
    Kept,
    Failed
};

/**
 * The user's saves in the cloud, game by game, for loading or deleting one at a time. A loaded save
 * gets its base, as one the sync brought down would. For the app thread only.
 */
class SaveBrowser
{
  public:
    /**
     * @param cloud where the backup is kept
     * @param state the base of every save
     * @param storage the console's hard disk
     * @param display what the user sees of a download, and asks before a save is replaced or
     * deleted
     * @param rootFolderId the cloud id of the Own Storage folder
     * @param savedata the user's savedata directory
     * @param ownSave a save folder of the app itself, which is never backed up
     * @param workDirectory where archives are kept while they move
     * @param target whom a save brought down is made to belong to; null to leave saves as they are
     */
    SaveBrowser(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
                const std::string &rootFolderId, const std::string &savedata,
                const std::string &ownSave, const std::string &workDirectory,
                const SaveTarget *target);
    SaveBrowser(const SaveBrowser &) = delete;
    SaveBrowser &operator=(const SaveBrowser &) = delete;

    /** Lists the saves in the cloud anew; false when they cannot be listed. */
    bool Refresh();
    /** The games with saves in the cloud, sorted by name from A to Z. */
    std::vector<CloudGame> Games() const;
    /**
     * A save's archive in the cloud.
     * @param name a save from Games()
     */
    const CloudSave &Save(const std::string &name) const;
    /**
     * How a save from Games() is on the console. Its archive is written to tell, unless the base
     * already says the console has the cloud's copy.
     */
    SaveStatus Status(const std::string &name);
    /**
     * Brings a save from Games() down; when the console has it with other content, only once the
     * user says the cloud's copy is to replace it.
     */
    LoadResult Load(const std::string &name);
    /**
     * Moves a save from Games() to the cloud's trash, once the user says so, and only when the
     * console does not have it; its base goes with it.
     */
    RemoveResult Remove(const std::string &name);

  private:
    SyncState &m_state;
    SyncDisplay &m_display;
    CloudStorage &m_cloud;
    const SaveScanner m_scanner;
    SaveArchives m_archives;
    const std::string m_rootFolderId;
    const std::string m_ownSave;
    std::map<std::string, CloudSave> m_saves;
};
