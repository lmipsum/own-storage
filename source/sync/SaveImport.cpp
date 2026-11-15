// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveImport.h"

#include "app/DebugLog.h"
#include "sync/CloudSaves.h"
#include "sync/ParamSfo.h"
#include "sync/SaveArchives.h"
#include "sync/SaveProgress.h"
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
    // Every drive is scanned before anything is uploaded, so the saves and their drives' archives
    // outlive the one run of the queue that uploads them all.
    std::deque<SaveArchives> drives;
    std::vector<std::vector<LocalSave>> saves;
    std::set<std::string> seen;
    for (const std::string &folder : folders)
    {
        drives.emplace_back(m_cloud, m_state, m_storage, m_display, folder, m_ownSave,
                            m_workDirectory, nullptr);
        saves.push_back(SaveScanner(m_storage, folder, m_ownSave).Scan());
    }
    const std::time_t read = m_clock.Seconds();
    debugPrintf("SaveImport::Run: the drives took %ld s\n", static_cast<long>(read - started));
    m_display.SetText(ProgressBar::Overall, "Listing the saves in the cloud...");
    std::map<std::string, CloudSave> cloudSaves;
    if (!ListCloudSaves(m_cloud, m_rootFolderId, m_ownSave, m_state, cloudSaves))
    {
        return false;
    }
    debugPrintf("SaveImport::Run: the cloud's list took %ld s\n",
                static_cast<long>(m_clock.Seconds() - read));
    std::vector<std::pair<SaveArchives *, const LocalSave *>> uploads;
    for (std::size_t drive = 0; drive < saves.size(); ++drive)
    {
        for (const LocalSave &save : saves[drive])
        {
            // A drive that was in a computer may hold files of its own there, such as
            // .DS_Store; every save has a PARAM.SFO.
            if (std::find(save.files.begin(), save.files.end(), paramSfoName) == save.files.end())
            {
                continue;
            }
            if (cloudSaves.count(save.name) != 0)
            {
                ++result.skipped;
            }
            else if (!seen.insert(save.name).second)
            {
                ++result.duplicates;
            }
            else
            {
                uploads.push_back(std::make_pair(&drives[drive], &save));
            }
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
