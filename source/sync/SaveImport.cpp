// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveImport.h"

#include "app/DebugLog.h"
#include "sync/CloudSaves.h"
#include "sync/ParamSfo.h"
#include "sync/SaveArchives.h"
#include "sync/SaveProgress.h"
#include "sync/SaveRegistry.h"
#include "sync/SaveScanner.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <utility>

SaveImport::SaveImport(CloudStorage &cloud, SyncState &state, LocalStorage &storage,
                       SyncDisplay &display, const Clock &clock, const std::string &rootFolderId,
                       const std::string &ownSave, const std::string &workDirectory)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display), m_clock(clock),
      m_rootFolderId(rootFolderId), m_ownSave(ownSave), m_workDirectory(workDirectory)
{
}

bool SaveImport::Run(const std::vector<std::string> &folders, ImportResult &result)
{
    result = ImportResult{0, 0, 0, 0};
    // How long the drives and the cloud's list take is logged, to tell which one to speed up.
    m_display.SetText(ProgressBar::Overall, "Reading the USB drive...");
    const std::time_t started = m_clock.Seconds();
    std::vector<std::vector<std::string>> driveNames;
    for (const std::string &folder : folders)
    {
        driveNames.push_back(SaveScanner(m_storage, folder, m_ownSave).Names());
    }
    const std::time_t named = m_clock.Seconds();
    m_display.SetText(ProgressBar::Overall, "Listing the saves in the cloud...");
    std::map<std::string, CloudSave> cloudSaves;
    if (!ListCloudSaves(m_cloud, m_rootFolderId, m_ownSave, m_state, cloudSaves))
    {
        return false;
    }
    const std::time_t listed = m_clock.Seconds();
    m_display.SetText(ProgressBar::Overall, "Reading the USB drive...");
    // Only the saves the cloud lacks are read file by file: on a slow drive that reading takes
    // the time, and a save the cloud has is left as it is. Every drive is read before anything is
    // uploaded, so the saves and their drives' archives outlive the one run of the queue that
    // uploads them all. An import only reads the drives' saves, so it writes no folder to register.
    NoSaveRegistry noRegistry;
    std::deque<SaveArchives> drives;
    std::vector<std::vector<LocalSave>> saves(folders.size());
    std::set<std::string> seen;
    for (std::size_t drive = 0; drive < folders.size(); ++drive)
    {
        drives.emplace_back(m_cloud, m_state, m_storage, noRegistry, m_display, folders[drive],
                            m_ownSave, m_workDirectory, nullptr);
        const SaveScanner scanner(m_storage, folders[drive], m_ownSave);
        for (const std::string &name : driveNames[drive])
        {
            if (cloudSaves.count(name) != 0)
            {
                ++result.skipped;
                continue;
            }
            LocalSave save = scanner.ScanSave(name);
            // A drive that was in a computer may hold files of its own there, such as
            // .DS_Store; every save has a PARAM.SFO.
            if (std::find(save.files.begin(), save.files.end(), paramSfoName) == save.files.end())
            {
                continue;
            }
            if (!seen.insert(name).second)
            {
                ++result.duplicates;
                continue;
            }
            saves[drive].push_back(save);
        }
    }
    debugPrintf(
        "SaveImport::Run: the names took %ld s, the cloud's list %ld s, the new saves %ld s\n",
        static_cast<long>(named - started), static_cast<long>(listed - named),
        static_cast<long>(m_clock.Seconds() - listed));
    std::vector<std::pair<SaveArchives *, const LocalSave *>> uploads;
    for (std::size_t drive = 0; drive < saves.size(); ++drive)
    {
        for (const LocalSave &save : saves[drive])
        {
            uploads.push_back(std::make_pair(&drives[drive], &save));
        }
    }
    unsigned long long bytes = 0;
    for (const auto &upload : uploads)
    {
        bytes += upload.second->bytes;
    }
    SaveProgress progress(m_display, m_clock, uploads.size(), bytes);
    std::vector<std::string> names;
    for (const auto &upload : uploads)
    {
        const LocalSave *save = upload.second;
        upload.first->QueueUpload(*save, m_rootFolderId, std::string(), true,
                                  [save, &result, &progress](const Json &item)
                                  {
                                      if (!item.Has("id"))
                                      {
                                          return;
                                      }
                                      debugPrintf("SaveImport::Run: %s stored\n",
                                                  save->name.c_str());
                                      ++result.imported;
                                      progress.Advance(save->bytes);
                                  });
        names.push_back(save->name);
    }
    if (!drives.empty())
    {
        drives.front().UploadQueue(names);
    }
    result.failed = uploads.size() - result.imported;
    return true;
}
