// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Sync.h"

#include "app/DebugLog.h"

#include <cstdio>
#include <ctime>
#include <time.h>
#include <optional>
#include <sstream>
#include <vector>

namespace
{
// remote.json's title in the cloud.
const char kStateTitle[] = "remote.json";

/**
 * Whether a downloaded state names only places inside the savedata directory: no absolute
 * path and no ".." step, which would let a restore write elsewhere.
 */
bool StaysInSavedata(const Json &state)
{
    for (const Json &entry : state["data"].AsArray())
    {
        const std::string path = entry["path"].Str();
        if (path.empty() || path[0] == '/' || ("/" + path + "/").find("/../") != std::string::npos)
        {
            return false;
        }
    }
    return true;
}

/** The percentage done of total, as a whole number; total must not be 0. */
unsigned int Percent(std::size_t done, std::size_t total)
{
    return static_cast<unsigned int>(done * 100 / total);
}
} // namespace

Sync::UploadProgress::UploadProgress(SyncDisplay &display, std::size_t total)
    : m_display(display), m_total(total), m_done(0), m_shownPercent(0)
{
}

void Sync::UploadProgress::SetLabel(const std::string &label)
{
    m_label = label;
    m_display.SetText(ProgressBar::Overall, label + "...");
}

void Sync::UploadProgress::Advance()
{
    ++m_done;
    std::stringstream text;
    text << m_label << ": " << m_done << " of " << m_total;
    m_display.SetText(ProgressBar::Overall, text.str());
    const unsigned int percent = Percent(m_done, m_total);
    m_display.Advance(ProgressBar::Overall, percent - m_shownPercent);
    m_shownPercent = percent;
}

Sync::Sync(CloudStorage &cloud, SyncState &state, LocalStorage &storage, SyncDisplay &display,
           const std::string &rootFolderId, const std::string &savedata)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_rootFolderId(rootFolderId), m_savedata(savedata)
{
}

RemoteStateFetch Sync::FetchState()
{
    Json stored;
    if (!m_cloud.FindInFolder(kStateTitle, ItemKind::File, m_rootFolderId, stored))
    {
        return RemoteStateFetch::Failed;
    }
    if (!stored.Has("id"))
    {
        debugPrintf("Sync::FetchState: there is no remote.json in the cloud\n");
        return RemoteStateFetch::Done;
    }
    debugPrintf("Sync::FetchState: downloading remote.json %s\n", stored["id"].Str().c_str());
    if (!m_cloud.DownloadFile(stored["id"].Str(), m_state.FilePath()))
    {
        return RemoteStateFetch::Failed;
    }
    const Json downloaded = Json::Load(m_state.FilePath());
    if (!downloaded.Has("data") || !StaysInSavedata(downloaded))
    {
        // Left on disk, the next run would take it for its own state.
        m_state.Discard();
        return RemoteStateFetch::Damaged;
    }
    m_state.Reset(downloaded);
    return RemoteStateFetch::Done;
}

void Sync::DetectChanges(const Json::Array &local)
{
    m_display.SetText(ProgressBar::Overall, "Detecting Changes...");
    m_display.SetText(ProgressBar::Current, "Finding file differences...");
    const std::size_t stateSize = m_state.Size();
    unsigned int shownPercent = 0;
    for (std::size_t i = 0; i < local.size() && !m_display.Cancelled(); i++)
    {
        const Json &entry = local[i];
        const std::string path = entry["path"].Str();
        bool known = false;
        for (std::size_t j = 0; j < stateSize; j++)
        {
            const Json stored = m_state.Entry(j);
            if (stored["path"].Str() != path)
            {
                continue;
            }
            known = true;
            if (entry["mimeType"].Str() == folderMime || stored["status"].Str() != syncedStatus)
            {
                continue;
            }
            // Only a file changed after it went up can differ; the MD5 tells whether it does.
            std::time_t changed;
            struct tm storedTime = {};
            strptime(stored["modifiedDate"].Str().substr(0, 18).c_str(), "%Y-%m-%dT%H:%M:%S",
                     &storedTime);
            const std::string localPath = LocalPath(entry);
            if (!m_storage.ModifiedTime(localPath, changed) ||
                std::difftime(changed, std::mktime(&storedTime)) <= 0)
            {
                continue;
            }
            const std::string md5 = m_storage.Md5(localPath);
            if (!md5.empty() && md5 != stored["md5Checksum"].Str())
            {
                debugPrintf("Sync::DetectChanges: %s changed\n", path.c_str());
                m_state.MarkChanged(j);
            }
        }
        if (!known)
        {
            debugPrintf("Sync::DetectChanges: %s is new\n", path.c_str());
            m_state.Append(entry);
        }

        std::stringstream text;
        text << "Finding File Differences: " << i << " of " << local.size();
        m_display.SetText(ProgressBar::Current, text.str());
        const unsigned int percent = Percent(i, local.size());
        m_display.Advance(ProgressBar::Overall, percent - shownPercent);
        m_display.Advance(ProgressBar::Current, percent - shownPercent);
        shownPercent = percent;
    }
}

void Sync::Upload()
{
    const std::size_t size = m_state.Size();
    std::size_t pending = 0;
    for (std::size_t i = 0; i < size; i++)
    {
        const Json entry = m_state.Entry(i);
        if (SyncState::IsPendingCreation(entry) || entry["status"].Str() == updateStatus)
        {
            ++pending;
        }
    }
    const CloudStorage::Stop stop = [this] { return Stopped(); };
    UploadProgress progress(m_display, pending);

    debugPrintf("Sync::Upload: %u entries to send\n", static_cast<unsigned int>(pending));
    m_display.Reset(ProgressBar::Overall);
    progress.SetLabel("Uploading Parent Entries");
    // Change detection leaves its own text and a full bar here.
    m_display.Reset(ProgressBar::Current);

    // The cloud finds a file by its parent folder's id, so folders go first, level by level:
    // each round creates the folders whose parent the rounds before have created.
    std::vector<bool> queued(size, false);
    for (std::size_t added = 1; added > 0;)
    {
        added = 0;
        for (std::size_t i = 0; i < size; i++)
        {
            const Json entry = m_state.Entry(i);
            if (queued[i] || !SyncState::IsPendingCreation(entry) ||
                entry["mimeType"].Str() != folderMime)
            {
                continue;
            }
            const std::optional<std::string> parentId = m_state.ParentId(entry, m_rootFolderId);
            if (parentId)
            {
                const std::string parent = *parentId;
                m_cloud.QueueFolder([this, i, parent, &progress](FolderToCreate &folder)
                                    { return PrepareFolder(i, parent, progress, folder); },
                                    [this, i, parent, &progress](const Json &folder)
                                    { FinishFolder(i, parent, progress, folder); });
                queued[i] = true;
                ++added;
            }
        }
        m_cloud.RunQueue(stop);
    }

    progress.SetLabel("Uploading Child Entries");
    for (std::size_t i = 0; i < size; i++)
    {
        const Json entry = m_state.Entry(i);
        if ((!SyncState::IsPendingCreation(entry) && entry["status"].Str() != updateStatus) ||
            entry["mimeType"].Str() == folderMime)
        {
            continue;
        }
        const std::optional<std::string> parentId = m_state.ParentId(entry, m_rootFolderId);
        if (parentId)
        {
            const std::string parent = *parentId;
            m_cloud.QueueFile(
                LocalPath(entry), [this, i, parent, &progress](FileToStore &file)
                { return PrepareFile(i, parent, progress, file); },
                [this, i, parent, &progress](const Json &file)
                { FinishFile(i, parent, progress, file); });
        }
    }
    m_cloud.RunQueue(stop);

    // The upload below sends remote.json as it is on disk, so it must hold every change.
    m_state.SaveAll();
    PushState();
}

void Sync::Download()
{
    const std::size_t size = m_state.Size();
    std::size_t missing = 0;
    for (std::size_t i = 0; i < size; i++)
    {
        const Json entry = m_state.Entry(i);
        if (entry["status"].Str() == syncedStatus && !m_storage.Exists(LocalPath(entry)))
        {
            ++missing;
        }
    }

    m_display.Reset(ProgressBar::Overall);
    std::size_t done = 0;
    unsigned int shownPercent = 0;
    // Folders first, so that every file has its directory.
    for (const bool folders : {true, false})
    {
        const char *const label =
            folders ? "Downloading Parent Entries" : "Downloading Child Entries";
        m_display.SetText(ProgressBar::Overall, std::string(label) + "...");
        for (std::size_t i = 0; i < size && !m_display.Cancelled(); i++)
        {
            const Json entry = m_state.Entry(i);
            const std::string path = LocalPath(entry);
            if (entry["status"].Str() != syncedStatus ||
                (entry["mimeType"].Str() == folderMime) != folders || m_storage.Exists(path))
            {
                continue;
            }
            ++done;
            debugPrintf("Sync::Download: %s\n", path.c_str());
            if (folders)
            {
                m_display.Reset(ProgressBar::Current);
                m_display.SetText(ProgressBar::Current, path);
                m_storage.MakeDirectory(path);
                m_display.Advance(ProgressBar::Current, 100);
            }
            else if (!m_cloud.DownloadFile(entry["id"].Str(), path))
            {
                continue;
            }
            std::stringstream text;
            text << label << ": " << done << " of " << missing;
            m_display.SetText(ProgressBar::Overall, text.str());
            const unsigned int percent = Percent(done, missing);
            m_display.Advance(ProgressBar::Overall, percent - shownPercent);
            shownPercent = percent;
        }
    }
    debugPrintf("Sync::Download: done; %u folders and files were missing\n",
                static_cast<unsigned int>(missing));
}

std::string Sync::LocalPath(const Json &entry) const
{
    return m_savedata + "/" + entry["path"].Str();
}

bool Sync::Stopped() const
{
    return m_display.Cancelled() || m_cloud.IsFull();
}

bool Sync::FindInterruptedCreation(const Json &entry, const std::string &parentId, Json &item)
{
    item = Json();
    if (entry["status"].Str() != creatingStatus)
    {
        return true;
    }
    const ItemKind kind = entry["mimeType"].Str() == folderMime ? ItemKind::Folder : ItemKind::File;
    if (!m_cloud.FindInFolder(entry["title"].Str(), kind, parentId, item))
    {
        debugPrintf("Sync::FindInterruptedCreation: %s left for the next run\n",
                    entry["path"].Str().c_str());
        return false;
    }
    if (item.Has("id"))
    {
        debugPrintf("Sync::FindInterruptedCreation: %s is already there from an interrupted run\n",
                    entry["path"].Str().c_str());
    }
    return true;
}

bool Sync::AdoptInterruptedFile(std::size_t index, const std::string &parentId,
                                UploadProgress &progress, std::string &fileId)
{
    const Json entry = m_state.Entry(index);
    fileId = entry["status"].Str() == updateStatus ? entry["id"].Str() : std::string();
    Json found;
    if (!FindInterruptedCreation(entry, parentId, found))
    {
        return true;
    }
    if (!found.Has("id"))
    {
        return false;
    }
    if (found["md5Checksum"].Str() == m_storage.Md5(LocalPath(entry)))
    {
        m_state.RecordFile(index, parentId, found);
        progress.Advance();
        return true;
    }
    // The interrupted upload may not hold the file's current content.
    fileId = found["id"].Str();
    return false;
}

void Sync::ShowRequestsInProgress(const char *kind, const std::string &title)
{
    std::stringstream text;
    text << m_cloud.Running() + 1 << " " << kind << " in progress, last: " << title;
    m_display.SetText(ProgressBar::Current, text.str());
}

bool Sync::PrepareFolder(std::size_t index, const std::string &parentId, UploadProgress &progress,
                         FolderToCreate &folder)
{
    const Json entry = m_state.Entry(index);
    debugPrintf("Sync::PrepareFolder: %s under %s\n", entry["title"].Str().c_str(),
                parentId.c_str());
    Json found;
    if (!FindInterruptedCreation(entry, parentId, found))
    {
        return false;
    }
    if (found.Has("id"))
    {
        m_state.RecordFolder(index, parentId, found);
        progress.Advance();
        return false;
    }
    m_state.MarkCreating(index);
    folder.title = entry["title"].Str();
    folder.parentId = parentId;
    ShowRequestsInProgress("folders", folder.title);
    return true;
}

void Sync::FinishFolder(std::size_t index, const std::string &parentId, UploadProgress &progress,
                        const Json &folder)
{
    if (!folder.Has("id"))
    {
        return;
    }
    debugPrintf("Sync::FinishFolder: created %s\n", folder["id"].Str().c_str());
    m_state.RecordFolder(index, parentId, folder);
    progress.Advance();
}

bool Sync::PrepareFile(std::size_t index, const std::string &parentId, UploadProgress &progress,
                       FileToStore &file)
{
    const Json entry = m_state.Entry(index);
    debugPrintf("Sync::PrepareFile: %s under %s\n", entry["title"].Str().c_str(), parentId.c_str());
    if (AdoptInterruptedFile(index, parentId, progress, file.fileId))
    {
        return false;
    }
    if (file.fileId.empty())
    {
        m_state.MarkCreating(index);
    }
    file.title = entry["title"].Str();
    file.parentId = parentId;
    ShowRequestsInProgress("files", file.title);
    return true;
}

void Sync::FinishFile(std::size_t index, const std::string &parentId, UploadProgress &progress,
                      const Json &file)
{
    if (!file.Has("id"))
    {
        return;
    }
    debugPrintf("Sync::FinishFile: stored %s\n", file["id"].Str().c_str());
    m_state.RecordFile(index, parentId, file);
    progress.Advance();
}

void Sync::PushState()
{
    Json stored;
    if (!m_cloud.FindInFolder(kStateTitle, ItemKind::File, m_rootFolderId, stored))
    {
        debugPrintf("Sync::PushState: remote.json left for the next run\n");
        return;
    }
    if (stored["md5Checksum"].Str() == m_storage.Md5(m_state.FilePath()))
    {
        return;
    }
    debugPrintf("Sync::PushState: uploading remote.json\n");
    // Without an id this creates remote.json; when the answer is lost, the next run finds it here.
    m_cloud.UploadFile(m_state.FilePath(), kStateTitle, jsonMime, m_rootFolderId,
                       stored["id"].Str());
}
