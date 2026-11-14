// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudSaves.h"
#include "sync/CloudStorage.h"
#include "sync/LocalStorage.h"
#include "sync/Resign.h"
#include "sync/SaveScanner.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <functional>
#include <string>
#include <vector>

/** What downloading a save came to. */
enum class Downloaded
{
    Failed,
    Done,
    /** Downloaded as it was, as it could not be made the target's; the game may refuse it. */
    DoneNotResigned
};

/**
 * A user's saves as archives: writing a save's archive, and downloading an archive from the
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
     * @param target whom a save downloaded is made to belong to; null to leave saves as they are
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
     * Downloads a save from the cloud. The archive's MD5 and every header are checked before
     * any file of the save is replaced, and the base says a download is under way until every file
     * is written, so a disk failure halfway is known to the next run. A save of another user or
     * console is made the target's; one that cannot be is still downloaded, as it is.
     * @param name the save
     * @param cloudSave its archive in the cloud
     * @return what came of it
     */
    Downloaded Download(const std::string &name, const CloudSave &cloudSave);
    /** The texts of a save's PARAM.SFO on the console; empty when it cannot be read. */
    SaveTexts LocalTexts(const std::string &name) const;
    /**
     * Queues a save's archive for upload; the archive is written when its turn comes and removed
     * once it is uploaded.
     * @param save the save; it must outlive the queue's run
     * @param folderId the cloud folder of the archives
     * @param fileId the archive to replace in the cloud; empty to create one
     * @param cloudOnly whether to mark it as kept in the cloud only
     * @param stored takes what the cloud says about the stored archive; without "id" when it
     * failed
     */
    void QueueUpload(const LocalSave &save, const std::string &folderId, const std::string &fileId,
                     bool cloudOnly, const std::function<void(const Json &)> &stored);
    /**
     * Uploads the cloud's whole queue, until all is uploaded, the user stops or the cloud is full;
     * the archives a stopped queue leaves behind are removed.
     * @param names the saves whose archives were queued, by this or by another SaveArchives with
     * the same work directory
     */
    void UploadQueue(const std::vector<std::string> &names);
    /** True when the user stopped or the cloud has no room left. */
    bool Stopped() const;

  private:
    /** Where a file's content lies in an archive. */
    struct ArchivedFile
    {
        std::string name;
        unsigned long long offset;
        unsigned long long size;
    };

    /**
     * Copies part of a file to the end of another, a piece at a time.
     * @param to the file to add to; Write() created it
     * @param from the file to copy from
     * @param offset where the part starts in from
     * @param size the part's bytes
     * @return false when from cannot be read whole, or to cannot be written
     */
    bool CopyPart(const std::string &to, const std::string &from, unsigned long long offset,
                  unsigned long long size);
    /**
     * Reads an archive's headers, and the content of the files a resign changes.
     * @param path the archive
     * @param files receives where each file lies in it
     * @param signing receives PARAM.SFO and PARAM.PFD, which are small
     * @return false when it is not a save's archive, is cut short, or holds a PARAM.SFO or
     * PARAM.PFD too large to be one
     */
    bool ListArchive(const std::string &path, std::vector<ArchivedFile> &files,
                     std::vector<TarFile> &signing) const;
    /**
     * Writes a save's files from its archive into the save folder.
     * @param folder the save folder
     * @param path the archive
     * @param files where each file lies in the archive
     * @param signing the files a resign changed, written as they are now
     * @return false when a file cannot be written
     */
    bool WriteSaveFiles(const std::string &folder, const std::string &path,
                        const std::vector<ArchivedFile> &files,
                        const std::vector<TarFile> &signing);

    CloudStorage &m_cloud;
    SyncState &m_state;
    LocalStorage &m_storage;
    SyncDisplay &m_display;
    const SaveScanner m_scanner;
    const std::string m_savedata;
    const std::string m_workDirectory;
    const SaveTarget *m_target;
};
