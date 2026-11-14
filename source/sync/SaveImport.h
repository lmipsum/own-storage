// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <cstddef>
#include <string>
#include <vector>

/** Where the console copies saves to on a USB drive, under the drive's root. */
const char usbSavedataDirectory[] = "/PS3/SAVEDATA";

/** What an import came to. */
struct ImportResult
{
    std::size_t imported;
    /** Saves the cloud had already under the same name, which keeps its copy. */
    std::size_t skipped;
    /** Saves on more than one drive, sent from the first one only. */
    std::size_t duplicates;
    /** Saves that did not go up, as one could not be read or the import was stopped. */
    std::size_t failed;
};

/**
 * Brings saves copied off the console, such as to a USB drive, into the cloud, so they need not
 * be kept there. They go among the user's saves marked as kept in the cloud only: no sync brings
 * them down by itself, and the user loads the ones wanted from the menu. For the app thread only.
 */
class SaveImport
{
  public:
    /**
     * @param cloud where the backup is kept
     * @param state the base of every save, which settles two archives of one save in the cloud
     * @param storage the console's disks
     * @param display what the user sees of the import
     * @param rootFolderId the cloud id of the Own Storage folder
     * @param ownSave a save folder of the app itself, which is never backed up
     * @param workDirectory where archives are kept while they move
     */
    SaveImport(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
               const std::string &rootFolderId, const std::string &ownSave,
               const std::string &workDirectory);
    SaveImport(const SaveImport &) = delete;
    SaveImport &operator=(const SaveImport &) = delete;

    /**
     * Sends up every save in the folders that the cloud has not got under its name.
     * @param folders folders that hold save folders, such as a USB drive's PS3/SAVEDATA
     * @param result receives what came of it
     * @return false when the cloud's saves could not be listed, so nothing was sent
     */
    bool Run(const std::vector<std::string> &folders, ImportResult &result);

  private:
    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    const std::string m_rootFolderId;
    const std::string m_ownSave;
    const std::string m_workDirectory;
};
