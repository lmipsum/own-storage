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
                       SyncDisplay &display, const std::string &rootFolderId,
                       const std::string &ownSave, const std::string &workDirectory)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_rootFolderId(rootFolderId), m_ownSave(ownSave), m_workDirectory(workDirectory)
{
}

bool SaveImport::Run(const std::vector<std::string> &folders, ImportResult &result)
{
    result = ImportResult{0, 0, 0, 0};
    m_display.SetText(ProgressBar::Overall, "Comparing saves...");
    std::map<std::string, CloudSave> cloudSaves;
    if (!ListCloudSaves(m_cloud, m_rootFolderId, m_ownSave, m_state, cloudSaves))
    {
        return false;
    }
    // Every drive is scanned before anything is sent, so the saves and their drives' archives
    // outlive the one run of the queue that sends them all.
    std::deque<SaveArchives> drives;
    std::vector<std::vector<LocalSave>> saves;
    std::set<std::string> seen;
    for (const std::string &folder : folders)
    {
        drives.emplace_back(m_cloud, m_state, m_storage, m_display, folder, m_ownSave,
                            m_workDirectory, nullptr);
        saves.push_back(SaveScanner(m_storage, folder, m_ownSave).Scan());
    }
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
    SaveProgress progress(m_display, uploads.size());
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
                                      progress.Advance();
                                  });
        names.push_back(save->name);
    }
    if (!drives.empty())
    {
        drives.front().SendQueue(names);
    }
    result.failed = uploads.size() - result.imported;
    return true;
}
