// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Sync.h"

#include "app/DebugLog.h"
#include "sync/Tar.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

namespace
{
// A save's archive in the cloud is named after the save folder.
const char kArchiveSuffix[] = ".tar";
const std::size_t kArchiveSuffixLength = sizeof kArchiveSuffix - 1;

/**
 * The save an archive title names; empty when the title is not a save archive, or names a folder
 * that would not stay in the savedata directory.
 */
std::string SaveName(const std::string &title)
{
    if (title.size() <= kArchiveSuffixLength ||
        title.compare(title.size() - kArchiveSuffixLength, kArchiveSuffixLength, kArchiveSuffix) !=
            0)
    {
        return std::string();
    }
    const std::string name = title.substr(0, title.size() - kArchiveSuffixLength);
    return IsArchivableName(name) ? name : std::string();
}
} // namespace

Sync::Progress::Progress(SyncDisplay &display, std::size_t total)
    : m_display(display), m_total(total), m_done(0), m_shownPercent(0)
{
    m_display.Reset(ProgressBar::Overall);
}

void Sync::Progress::Advance()
{
    ++m_done;
    std::ostringstream text;
    text << "Saves: " << m_done << " of " << m_total;
    m_display.SetText(ProgressBar::Overall, text.str());
    const unsigned int percent = static_cast<unsigned int>(m_done * 100 / m_total);
    m_display.Advance(ProgressBar::Overall, percent - m_shownPercent);
    m_shownPercent = percent;
}

Sync::Sync(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
           const std::string &rootFolderId, const std::string &savedata, const std::string &ownSave,
           const std::string &workDirectory)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_scanner(storage, savedata, ownSave), m_rootFolderId(rootFolderId), m_savedata(savedata),
      m_ownSave(ownSave), m_workDirectory(workDirectory)
{
}

bool Sync::Run()
{
    m_display.SetText(ProgressBar::Overall, "Comparing saves...");
    std::map<std::string, CloudSave> cloudSaves;
    if (!ListCloudSaves(cloudSaves))
    {
        return false;
    }
    std::map<std::string, LocalSave> localSaves;
    for (const LocalSave &save : m_scanner.Scan())
    {
        localSaves[save.name] = save;
    }
    std::set<std::string> names;
    for (const auto &save : localSaves)
    {
        names.insert(save.first);
    }
    for (const auto &save : cloudSaves)
    {
        names.insert(save.first);
    }
    for (const std::string &name : m_state.Names())
    {
        names.insert(name);
    }

    std::vector<std::pair<const LocalSave *, std::string>> uploads;
    std::vector<std::pair<std::string, CloudSave>> downloads;
    for (const std::string &name : names)
    {
        const SaveBase *base = m_state.Find(name);
        const auto local = localSaves.find(name);
        const auto cloud = cloudSaves.find(name);
        const bool onConsole = local != localSaves.end();
        const bool inCloud = cloud != cloudSaves.end();
        if (!onConsole && !inCloud)
        {
            m_state.Forget(name);
        }
        else if (base != nullptr && base->downloading)
        {
            // An earlier run ended while writing the save; the cloud's copy is the one to have.
            if (inCloud)
            {
                downloads.push_back(*cloud);
            }
            else
            {
                debugPrintf("Sync::Run: %s was left half written and is gone from the cloud\n",
                            name.c_str());
            }
        }
        else if (!inCloud)
        {
            // New on the console, or gone from the cloud while the console still has it.
            uploads.push_back(std::make_pair(&local->second, std::string()));
        }
        else if (!onConsole)
        {
            if (base == nullptr)
            {
                downloads.push_back(*cloud);
            }
            else
            {
                debugPrintf("Sync::Run: %s was deleted on the console, left as it is\n",
                            name.c_str());
            }
        }
        else
        {
            const bool localChanged =
                base == nullptr || local->second.fingerprint != base->fingerprint;
            const bool cloudChanged = base == nullptr || cloud->second.md5 != base->md5;
            if (!localChanged && cloudChanged)
            {
                downloads.push_back(*cloud);
            }
            else if (localChanged)
            {
                // A changed fingerprint may still mean the same content; the archive tells.
                // It is removed at once and written again when its turn to go up comes, so
                // archives are not all on the disk at the same time.
                const std::string path = ArchivePath(name);
                const std::string md5 = WriteArchive(local->second, path);
                m_storage.Remove(path);
                if (!md5.empty() && md5 == cloud->second.md5)
                {
                    m_state.Record(name, SaveBase{cloud->second.fileId, md5,
                                                  local->second.fingerprint, false});
                }
                else if (!md5.empty() && !cloudChanged)
                {
                    uploads.push_back(std::make_pair(&local->second, cloud->second.fileId));
                }
                else if (!md5.empty())
                {
                    debugPrintf("Sync::Run: %s changed on both sides, left as it is\n",
                                name.c_str());
                }
            }
        }
    }

    Progress progress(m_display, uploads.size() + downloads.size());
    debugPrintf("Sync::Run: %u saves to send, %u to bring down\n",
                static_cast<unsigned int>(uploads.size()),
                static_cast<unsigned int>(downloads.size()));
    for (const auto &upload : uploads)
    {
        QueueUpload(*upload.first, upload.second, progress);
    }
    m_cloud.RunQueue([this] { return Stopped(); });
    // A stopped queue leaves the archives it had written behind.
    for (const auto &upload : uploads)
    {
        m_storage.Remove(ArchivePath(upload.first->name));
    }
    // A full cloud stops only uploads; bringing saves down needs no room there.
    for (const auto &download : downloads)
    {
        if (m_display.Cancelled())
        {
            break;
        }
        if (Download(download.first, download.second))
        {
            progress.Advance();
        }
    }
    return true;
}

bool Sync::ListCloudSaves(std::map<std::string, CloudSave> &saves)
{
    Json::Array files;
    if (!m_cloud.ListFiles(m_rootFolderId, files))
    {
        return false;
    }
    for (const Json &file : files)
    {
        const std::string name = SaveName(file["title"].Str());
        if (name.empty() || name == m_ownSave)
        {
            continue;
        }
        const CloudSave save{file["id"].Str(), file["md5Checksum"].Str()};
        const auto known = saves.find(name);
        if (known == saves.end())
        {
            saves.insert(std::make_pair(name, save));
            continue;
        }
        debugPrintf("Sync::ListCloudSaves: %s is in the cloud twice\n", name.c_str());
        const SaveBase *base = m_state.Find(name);
        const bool baseNamesThis = base != nullptr && base->fileId == save.fileId;
        const bool baseNamesKnown = base != nullptr && base->fileId == known->second.fileId;
        if (baseNamesThis || (!baseNamesKnown && save.fileId < known->second.fileId))
        {
            known->second = save;
        }
    }
    return true;
}

std::string Sync::WriteArchive(const LocalSave &save, const std::string &path)
{
    std::vector<TarFile> files;
    for (const std::string &name : save.files)
    {
        TarFile file{name, std::string()};
        if (!IsArchivableName(name) ||
            !m_storage.Read(m_savedata + "/" + save.name + "/" + name, file.content))
        {
            debugPrintf("Sync::WriteArchive: %s/%s cannot be archived\n", save.name.c_str(),
                        name.c_str());
            return std::string();
        }
        // A save can be large; its content is moved, never copied.
        files.push_back(std::move(file));
    }
    if (!m_storage.Write(path, MakeTar(std::move(files))))
    {
        debugPrintf("Sync::WriteArchive: %s cannot be written\n", path.c_str());
        return std::string();
    }
    return m_storage.Md5(path);
}

std::string Sync::ArchivePath(const std::string &name) const
{
    return m_workDirectory + "/" + name + kArchiveSuffix;
}

void Sync::QueueUpload(const LocalSave &save, const std::string &fileId, Progress &progress)
{
    const std::string path = ArchivePath(save.name);
    m_cloud.QueueFile(
        path,
        [this, &save, fileId, path](FileToStore &file)
        {
            if (WriteArchive(save, path).empty())
            {
                return false;
            }
            file.title = save.name + kArchiveSuffix;
            file.parentId = m_rootFolderId;
            file.fileId = fileId;
            std::ostringstream text;
            text << m_cloud.Running() + 1 << " saves in progress, last: " << save.name;
            m_display.SetText(ProgressBar::Current, text.str());
            return true;
        },
        [this, &save, path, &progress](const Json &stored)
        {
            m_storage.Remove(path);
            if (!stored.Has("id"))
            {
                return;
            }
            debugPrintf("Sync::QueueUpload: %s stored\n", save.name.c_str());
            m_state.Record(save.name, SaveBase{stored["id"].Str(), stored["md5Checksum"].Str(),
                                               save.fingerprint, false});
            progress.Advance();
        });
}

bool Sync::Download(const std::string &name, const CloudSave &cloudSave)
{
    m_display.SetText(ProgressBar::Current, name);
    const std::string path = ArchivePath(name);
    std::string archive;
    std::vector<TarFile> files;
    const bool whole = m_cloud.DownloadFile(cloudSave.fileId, path) &&
                       m_storage.Md5(path) == cloudSave.md5 && m_storage.Read(path, archive) &&
                       ReadTar(archive, files);
    m_storage.Remove(path);
    // The files hold their own copy now; the console's memory is small.
    std::string().swap(archive);
    if (!whole)
    {
        debugPrintf("Sync::Download: %s could not be brought down whole\n", name.c_str());
        return false;
    }

    m_state.Record(name, SaveBase{cloudSave.fileId, cloudSave.md5, std::string(), true});
    const std::string folder = m_savedata + "/" + name;
    m_storage.MakeDirectory(folder);
    const LocalSave before = m_scanner.ScanSave(name);
    for (const TarFile &file : files)
    {
        if (!m_storage.Write(folder + "/" + file.name, file.content))
        {
            debugPrintf("Sync::Download: %s/%s cannot be written\n", name.c_str(),
                        file.name.c_str());
            return false;
        }
    }
    for (const std::string &old : before.files)
    {
        const bool kept = std::any_of(files.begin(), files.end(),
                                      [&old](const TarFile &file) { return file.name == old; });
        if (!kept)
        {
            m_storage.Remove(folder + "/" + old);
        }
    }
    m_state.Record(name, SaveBase{cloudSave.fileId, cloudSave.md5,
                                  m_scanner.ScanSave(name).fingerprint, false});
    debugPrintf("Sync::Download: %s saved\n", name.c_str());
    return true;
}

bool Sync::Stopped() const
{
    return m_display.Cancelled() || m_cloud.IsFull();
}
