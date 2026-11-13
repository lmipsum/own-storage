// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

#include <string>
#include <vector>

/** Where every user of the console is a directory, named by the user's number. */
const std::string usersDirectory = "/dev_hdd0/home";

/**
 * A user's savedata directory on the console.
 * @param user the user's folder name under /dev_hdd0/home, such as "00000001"
 * @return the directory
 */
std::string SavedataDirectory(const std::string &user);

/** A save folder on the console. */
struct LocalSave
{
    /** The folder's name in the savedata directory. */
    std::string name;
    /** The files in it, sorted by name. */
    std::vector<std::string> files;
    /**
     * Every file's name, size and time of change: it stays the same while the save does, so
     * the save needs no reading to tell that.
     */
    std::string fingerprint;
};

/** Finds what there is to back up of one user: the save folders. */
class SaveScanner
{
  public:
    /**
     * @param storage the console's hard disk
     * @param savedata the user's savedata directory
     * @param ownSave a save folder of the app itself, which is never backed up
     */
    SaveScanner(const LocalStorage &storage, const std::string &savedata,
                const std::string &ownSave);

    /** The user's save folders, sorted by name. */
    std::vector<LocalSave> Scan() const;
    /**
     * One save folder as it is now.
     * @param name the folder's name in the savedata directory
     * @return the save; without files when the folder is empty or not there
     */
    LocalSave ScanSave(const std::string &name) const;

  private:
    const LocalStorage &m_storage;
    const std::string m_savedata;
    const std::string m_ownSave;
};
