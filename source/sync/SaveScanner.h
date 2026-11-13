// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"
#include "sync/LocalStorage.h"

#include <string>

/** Where every user of the console is a directory, named by the user's number. */
const std::string usersDirectory = "/dev_hdd0/home";

/**
 * A user's savedata directory on the console.
 * @param user the user's folder name under /dev_hdd0/home, such as "00000001"
 * @return the directory
 */
std::string SavedataDirectory(const std::string &user);

/** Finds what there is to back up of one user: the save folders and their files. */
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

    /**
     * Lists the user's saves as remote.json entries that are still to be uploaded: the save
     * folders first, then the files in those. Their paths are relative to the savedata directory,
     * so a backup belongs to the person, whatever number the user has on a console.
     * @return the entries
     */
    Json::Array Scan() const;

  private:
    const LocalStorage &m_storage;
    const std::string m_savedata;
    const std::string m_ownSave;
};
