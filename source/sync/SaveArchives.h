// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudSaves.h"
#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/Resign.h"
#include "sync/SaveScanner.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <string>

/** What bringing a save down came to. */
enum class BroughtDown
{
    Failed,
    Done,
    /** Brought down as it was, as it could not be made the target's; the game may refuse it. */
    DoneNotResigned
};

/**
 * A user's saves as archives: writing a save's archive, and bringing an archive down from the
 * cloud into its save folder, made the user's own on the way. For the app thread only.
 */
class SaveArchives
{
  public:
    /**
     * @param cloud where the archives are kept
     * @param state the base of every save
     * @param storage the console's hard disk
     * @param display what the user sees of a download
     * @param savedata the user's savedata directory
     * @param ownSave a save folder of the app itself, which is never backed up
     * @param workDirectory where archives are kept while they move
     * @param target whom a save brought down is made to belong to; null to leave saves as they are
     */
    SaveArchives(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
                 const std::string &savedata, const std::string &ownSave,
                 const std::string &workDirectory, const SaveTarget *target);
    SaveArchives(const SaveArchives &) = delete;
    SaveArchives &operator=(const SaveArchives &) = delete;

    /** Where a save's archive is kept while it moves. */
    std::string ArchivePath(const std::string &name) const;
    /**
     * Writes a save's archive.
     * @param save the save
     * @param path where the archive goes
     * @return the archive's MD5; empty when a file of the save cannot be read or archived, or
     * the archive cannot be written
     */
    std::string WriteArchive(const LocalSave &save, const std::string &path);
    /**
     * The MD5 a save's archive has now; the archive is written to tell and removed at once.
     * @return the MD5; empty when the archive cannot be written
     */
    std::string ArchiveMd5(const LocalSave &save);
    /**
     * Brings a save down from the cloud. The archive is checked whole before any file of the save
     * is replaced, and the base says a download is under way until every file is written. A save of
     * another user or console is made the target's; one that cannot be still comes down, as it is.
     * @param name the save
     * @param cloudSave its archive in the cloud
     * @return what came of it
     */
    BroughtDown BringDown(const std::string &name, const CloudSave &cloudSave);
    /** The game title in a save's PARAM.SFO on the console; empty when it cannot be read. */
    std::string LocalTitle(const std::string &name) const;

  private:
    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    const SaveScanner m_scanner;
    const std::string m_savedata;
    const std::string m_workDirectory;
    const SaveTarget *m_target;
};
