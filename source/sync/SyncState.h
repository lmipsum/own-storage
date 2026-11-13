// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

/** The folder in the user's Drive that holds every backup. */
const std::string driveFolderName = "Own Storage";

/** What a save was like, on the console and in the cloud, when the two last held the same. */
struct SaveBase
{
    /** The cloud id of the save's archive. */
    std::string fileId;
    /** The archive's MD5. */
    std::string md5;
    /** LocalSave::fingerprint of the save folder then. */
    std::string fingerprint;
    /**
     * True while the archive's files are being written into the save folder: a run that ends
     * before they all are leaves it set, and the next run brings the save down again.
     */
    bool downloading;
};

/**
 * The base of every save: what it was like when the console and the cloud last held the same.
 * A change since then, on either side, is what the sync acts on. Saved in full after every
 * change, with a backup. For the sync thread only.
 */
class SyncState
{
  public:
    /**
     * @param file where the state is kept
     * @param backup the copy every save writes next to it
     */
    SyncState(const std::string &file, const std::string &backup);
    SyncState(const SyncState &) = delete;
    SyncState &operator=(const SyncState &) = delete;

    /**
     * Reads the state, or its backup.
     * @return false when neither file holds a state; there are then no bases
     */
    bool Load();

    /** The names of the saves that have a base. */
    std::vector<std::string> Names() const;
    /**
     * The base of a save.
     * @param name the save folder's name
     * @return the base; null when the save has none
     */
    const SaveBase *Find(const std::string &name) const;
    /** Sets the base of a save and saves the state. */
    void Record(const std::string &name, const SaveBase &base);
    /** Drops the base of a save and saves the state. */
    void Forget(const std::string &name);

  private:
    /** Writes the state and its backup. */
    void Save() const;

    std::string m_file;
    std::string m_backup;
    std::map<std::string, SaveBase> m_bases;
};
