// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveArchives.h"

#include "app/DebugLog.h"
#include "sync/ParamSfo.h"
#include "sync/Tar.h"

#include <algorithm>
#include <utility>
#include <vector>

SaveArchives::SaveArchives(CloudStorage &cloud, SyncState &state, LocalStorage &storage,
                           SyncDisplay &display, const std::string &savedata,
                           const std::string &ownSave, const std::string &workDirectory)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_scanner(storage, savedata, ownSave), m_savedata(savedata), m_workDirectory(workDirectory)
{
}

std::string SaveArchives::ArchivePath(const std::string &name) const
{
    return m_workDirectory + "/" + ArchiveTitle(name);
}

std::string SaveArchives::WriteArchive(const LocalSave &save, const std::string &path)
{
    std::vector<TarFile> files;
    for (const std::string &name : save.files)
    {
        TarFile file{name, std::string()};
        if (!IsArchivableName(name) ||
            !m_storage.Read(m_savedata + "/" + save.name + "/" + name, file.content))
        {
            debugPrintf("SaveArchives::WriteArchive: %s/%s cannot be archived\n", save.name.c_str(),
                        name.c_str());
            return std::string();
        }
        // A save can be large; its content is moved, never copied.
        files.push_back(std::move(file));
    }
    if (!m_storage.Write(path, MakeTar(std::move(files))))
    {
        debugPrintf("SaveArchives::WriteArchive: %s cannot be written\n", path.c_str());
        return std::string();
    }
    return m_storage.Md5(path);
}

std::string SaveArchives::ArchiveMd5(const LocalSave &save)
{
    const std::string path = ArchivePath(save.name);
    const std::string md5 = WriteArchive(save, path);
    m_storage.Remove(path);
    return md5;
}

bool SaveArchives::BringDown(const std::string &name, const CloudSave &cloudSave)
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
        debugPrintf("SaveArchives::BringDown: %s could not be brought down whole\n", name.c_str());
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
            debugPrintf("SaveArchives::BringDown: %s/%s cannot be written\n", name.c_str(),
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
    debugPrintf("SaveArchives::BringDown: %s saved\n", name.c_str());
    return true;
}

std::string SaveArchives::LocalTitle(const std::string &name) const
{
    std::string sfo;
    return m_storage.Read(m_savedata + "/" + name + "/" + paramSfoName, sfo) ? ReadSfoTitle(sfo)
                                                                             : std::string();
}
