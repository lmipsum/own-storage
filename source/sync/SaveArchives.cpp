// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveArchives.h"

#include "app/DebugLog.h"
#include "sync/GameName.h"
#include "sync/ParamPfd.h"
#include "sync/ParamSfo.h"
#include "sync/Tar.h"

#include <algorithm>
#include <ctime>
#include <utility>
#include <vector>

namespace
{
// What one read or write of a large file moves: small next to the console's memory, which a
// whole save could fill.
const unsigned long long kPieceBytes = 1024 * 1024;
// More than a PARAM.SFO or a PARAM.PFD ever holds; a larger one would fill the console's memory.
const unsigned long long kSigningFileLimit = 64 * 1024;

/**
 * A time as Drive writes modifiedDate.
 * @param time seconds since 1970, UTC
 * @return the time, such as "2026-11-14T10:56:21.000Z"; empty when it is not known
 */
std::string CloudTime(std::time_t time)
{
    const std::tm *utc = time > 0 ? std::gmtime(&time) : nullptr;
    char text[32];
    if (utc == nullptr || std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.000Z", utc) == 0)
    {
        return std::string();
    }
    return text;
}
} // namespace

SaveArchives::SaveArchives(CloudStorage &cloud, SyncState &state, LocalStorage &storage,
                           SyncDisplay &display, const std::string &savedata,
                           const std::string &ownSave, const std::string &workDirectory,
                           const SaveTarget *target)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_scanner(storage, savedata, ownSave), m_savedata(savedata), m_workDirectory(workDirectory),
      m_target(target)
{
}

std::string SaveArchives::ArchivePath(const std::string &name) const
{
    return m_workDirectory + "/" + ArchiveTitle(name);
}

std::string SaveArchives::WriteArchive(const LocalSave &save, const std::string &path)
{
    // Written a file at a time, so a large save never sits in memory whole; the files come
    // sorted by name, as an archive has them.
    if (!m_storage.Write(path, std::string()))
    {
        debugPrintf("SaveArchives::WriteArchive: %s cannot be written\n", path.c_str());
        return std::string();
    }
    for (const std::string &name : save.files)
    {
        const std::string file = m_savedata + "/" + save.name + "/" + name;
        FileStatus status;
        if (!IsArchivableName(name) || !m_storage.Status(file, status) ||
            !m_storage.Append(path, TarHeader(name, status.size)) ||
            !CopyPart(path, file, 0, status.size) ||
            !m_storage.Append(path, std::string(TarPadding(status.size), '\0')))
        {
            debugPrintf("SaveArchives::WriteArchive: %s/%s cannot be archived\n", save.name.c_str(),
                        name.c_str());
            m_storage.Remove(path);
            return std::string();
        }
    }
    if (!m_storage.Append(path, TarEnd()))
    {
        debugPrintf("SaveArchives::WriteArchive: %s cannot be written\n", path.c_str());
        m_storage.Remove(path);
        return std::string();
    }
    return m_storage.Md5(path);
}

bool SaveArchives::CopyPart(const std::string &to, const std::string &from,
                            unsigned long long offset, unsigned long long size)
{
    std::string piece;
    for (unsigned long long done = 0; done < size; done += piece.size())
    {
        const std::size_t bytes = static_cast<std::size_t>(std::min(kPieceBytes, size - done));
        if (!m_storage.ReadPart(from, offset + done, bytes, piece) || !m_storage.Append(to, piece))
        {
            return false;
        }
    }
    return true;
}

bool SaveArchives::ListArchive(const std::string &path, std::vector<ArchivedFile> &files,
                               std::vector<TarFile> &signing) const
{
    FileStatus archive;
    if (!m_storage.Status(path, archive))
    {
        return false;
    }
    std::string block;
    unsigned long long offset = 0;
    while (m_storage.ReadPart(path, offset, kTarBlock, block))
    {
        ArchivedFile file{std::string(), 0, 0};
        const TarBlock read = ReadTarHeader(block, file.name, file.size);
        if (read != TarBlock::File)
        {
            return read == TarBlock::End;
        }
        file.offset = offset + kTarBlock;
        if (file.size > archive.size - file.offset)
        {
            return false;
        }
        if (file.name == paramSfoName || file.name == paramPfdName)
        {
            TarFile small{file.name, std::string()};
            if (file.size > kSigningFileLimit)
            {
                return false;
            }
            if (!m_storage.ReadPart(path, file.offset, static_cast<std::size_t>(file.size),
                                    small.content))
            {
                return false;
            }
            signing.push_back(std::move(small));
        }
        offset = file.offset + file.size + TarPadding(file.size);
        files.push_back(std::move(file));
    }
    // Without the end blocks the archive was cut short.
    return false;
}

std::string SaveArchives::ArchiveMd5(const LocalSave &save)
{
    const std::string path = ArchivePath(save.name);
    const std::string md5 = WriteArchive(save, path);
    m_storage.Remove(path);
    return md5;
}

Downloaded SaveArchives::Download(const std::string &name, const CloudSave &cloudSave)
{
    m_display.SetText(ProgressBar::Current,
                      "Downloading " + GameName(TitleId(name), cloudSave.texts.title));
    const std::string path = ArchivePath(name);
    std::vector<ArchivedFile> files;
    // Only these are held in memory: a resign changes them, and they are small.
    std::vector<TarFile> signing;
    const bool whole = m_cloud.DownloadFile(cloudSave.fileId, path) &&
                       m_storage.Md5(path) == cloudSave.md5 && ListArchive(path, files, signing);
    if (!whole)
    {
        m_storage.Remove(path);
        debugPrintf("SaveArchives::Download: %s could not be downloaded whole\n", name.c_str());
        return Downloaded::Failed;
    }

    const bool resignRefused =
        m_target != nullptr && ResignSave(signing, *m_target) == ResignResult::Refused;
    if (resignRefused)
    {
        debugPrintf("SaveArchives::Download: %s cannot be made this user's\n", name.c_str());
    }
    m_state.Record(name, SaveBase{cloudSave.fileId, cloudSave.md5, std::string(), true});
    const std::string folder = m_savedata + "/" + name;
    m_storage.MakeDirectory(folder);
    const LocalSave before = m_scanner.ScanSave(name);
    const bool written = WriteSaveFiles(folder, path, files, signing);
    m_storage.Remove(path);
    if (!written)
    {
        return Downloaded::Failed;
    }
    for (const std::string &old : before.files)
    {
        const bool kept = std::any_of(files.begin(), files.end(), [&old](const ArchivedFile &file)
                                      { return file.name == old; });
        if (!kept)
        {
            m_storage.Remove(folder + "/" + old);
        }
    }
    m_state.Record(name, SaveBase{cloudSave.fileId, cloudSave.md5,
                                  m_scanner.ScanSave(name).fingerprint, false});
    debugPrintf("SaveArchives::Download: %s saved\n", name.c_str());
    return resignRefused ? Downloaded::DoneNotResigned : Downloaded::Done;
}

bool SaveArchives::WriteSaveFiles(const std::string &folder, const std::string &path,
                                  const std::vector<ArchivedFile> &files,
                                  const std::vector<TarFile> &signing)
{
    for (const ArchivedFile &file : files)
    {
        const std::string target = folder + "/" + file.name;
        const auto held = std::find_if(signing.begin(), signing.end(), [&file](const TarFile &each)
                                       { return each.name == file.name; });
        const bool written = held != signing.end()
                                 ? m_storage.Write(target, held->content)
                                 : m_storage.Write(target, std::string()) &&
                                       CopyPart(target, path, file.offset, file.size);
        if (!written)
        {
            debugPrintf("SaveArchives::WriteSaveFiles: %s/%s cannot be written\n", folder.c_str(),
                        file.name.c_str());
            return false;
        }
    }
    return true;
}

SaveTexts SaveArchives::LocalTexts(const std::string &name) const
{
    std::string sfo;
    return m_storage.Read(m_savedata + "/" + name + "/" + paramSfoName, sfo) ? ReadSfoTexts(sfo)
                                                                             : SaveTexts();
}

void SaveArchives::QueueUpload(const LocalSave &save, const std::string &folderId,
                               const std::string &fileId, bool cloudOnly,
                               const std::function<void(const Json &)> &stored)
{
    const std::string path = ArchivePath(save.name);
    m_cloud.QueueFile(
        path,
        [this, &save, folderId, fileId, cloudOnly, path](FileToStore &file)
        {
            if (WriteArchive(save, path).empty())
            {
                return false;
            }
            const SaveTexts texts = LocalTexts(save.name);
            // Every console can then show what the save is without downloading the archive,
            // and the cloud dates the save as the console does, not by its upload.
            file = FileToStore{
                ArchiveTitle(save.name), DescribeSave(texts), folderId, fileId, cloudOnly,
                CloudTime(save.modified)};
            m_display.SetText(ProgressBar::Current,
                              "Uploading " + GameName(TitleId(save.name), texts.title));
            return true;
        },
        [this, path, stored](const Json &item)
        {
            m_storage.Remove(path);
            stored(item);
        });
}

void SaveArchives::UploadQueue(const std::vector<std::string> &names)
{
    m_cloud.RunQueue([this] { return Stopped(); });
    for (const std::string &name : names)
    {
        m_storage.Remove(ArchivePath(name));
    }
}

bool SaveArchives::Stopped() const
{
    return m_display.Cancelled() || m_cloud.IsFull();
}
