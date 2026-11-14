// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveBrowser.h"

#include "app/DebugLog.h"
#include "sync/GameName.h"

#include <algorithm>
#include <cctype>

namespace
{
/** Whether a comes before b from A to Z, whatever the letters' case. */
bool LessIgnoringCase(const std::string &a, const std::string &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y)
                                        {
                                            return std::tolower(static_cast<unsigned char>(x)) <
                                                   std::tolower(static_cast<unsigned char>(y));
                                        });
}
} // namespace

SaveBrowser::SaveBrowser(CloudStorage &cloud, SyncState &state, LocalStorage &storage,
                         SyncDisplay &display, const std::string &rootFolderId,
                         const std::string &savedata, const std::string &ownSave,
                         const std::string &workDirectory, const SaveTarget *target)
    : m_state(state), m_display(display), m_cloud(cloud), m_scanner(storage, savedata, ownSave),
      m_archives(cloud, state, storage, display, savedata, ownSave, workDirectory, target),
      m_rootFolderId(rootFolderId), m_ownSave(ownSave)
{
}

bool SaveBrowser::Refresh()
{
    return ListCloudSaves(m_cloud, m_rootFolderId, m_ownSave, m_state, m_saves);
}

std::vector<CloudGame> SaveBrowser::Games() const
{
    std::vector<CloudGame> games;
    std::string title;
    // The saves are sorted by name, so the saves of a game come one after the other.
    for (const auto &save : m_saves)
    {
        const std::string titleId = TitleId(save.first);
        if (games.empty() || games.back().titleId != titleId)
        {
            title.clear();
            games.push_back(CloudGame{titleId, titleId, titleId, std::vector<std::string>()});
        }
        if (title.empty())
        {
            title =
                save.second.title.empty() ? m_archives.LocalTitle(save.first) : save.second.title;
            games.back().name = GameName(titleId, title);
            games.back().label = GameLabel(titleId, title);
        }
        games.back().saves.push_back(save.first);
    }
    // RFC 3339 times in one form sort as text does.
    for (CloudGame &game : games)
    {
        std::stable_sort(game.saves.begin(), game.saves.end(),
                         [this](const std::string &a, const std::string &b)
                         { return Save(a).modified > Save(b).modified; });
    }
    std::stable_sort(games.begin(), games.end(),
                     [](const CloudGame &a, const CloudGame &b)
                     {
                         return LessIgnoringCase(a.name, b.name) ||
                                (!LessIgnoringCase(b.name, a.name) && a.titleId < b.titleId);
                     });
    return games;
}

const CloudSave &SaveBrowser::Save(const std::string &name) const
{
    return m_saves.at(name);
}

SaveStatus SaveBrowser::Status(const std::string &name)
{
    const CloudSave &cloud = Save(name);
    const LocalSave local = m_scanner.ScanSave(name);
    if (local.files.empty())
    {
        return SaveStatus::Missing;
    }
    const SaveBase *base = m_state.Find(name);
    const bool baseSaysSame = base != nullptr && !base->downloading &&
                              base->fileId == cloud.fileId && base->md5 == cloud.md5 &&
                              base->fingerprint == local.fingerprint;
    if (baseSaysSame || m_archives.ArchiveMd5(local) == cloud.md5)
    {
        return SaveStatus::Same;
    }
    return SaveStatus::Different;
}

LoadResult SaveBrowser::Load(const std::string &name)
{
    const SaveStatus status = Status(name);
    if (status == SaveStatus::Same)
    {
        return LoadResult::AlreadyThere;
    }
    if (status == SaveStatus::Different)
    {
        const std::string local = m_archives.LocalTitle(name);
        const std::string game = GameLabel(TitleId(name), local.empty() ? Save(name).title : local);
        if (m_display.Ask(game + ":\n" + name +
                          " on this PS3 differs from the cloud's copy.\n\n"
                          "Yes: the cloud's copy replaces it.\nNo: keep this PS3's.") !=
            Answer::Yes)
        {
            return LoadResult::Kept;
        }
    }
    debugPrintf("SaveBrowser::Load: %s\n", name.c_str());
    switch (m_archives.BringDown(name, Save(name)))
    {
    case BroughtDown::Done:
        return LoadResult::Loaded;
    case BroughtDown::DoneNotResigned:
        return LoadResult::LoadedNotResigned;
    default:
        return LoadResult::Failed;
    }
}

RemoveResult SaveBrowser::Remove(const std::string &name)
{
    if (!m_scanner.ScanSave(name).files.empty())
    {
        return RemoveResult::OnConsole;
    }
    const CloudSave &save = Save(name);
    // The console has no PARAM.SFO of it to read the title from.
    const std::string game = GameLabel(TitleId(name), save.title);
    if (m_display.Ask(game + ":\n" + name +
                      " is not on this PS3. Delete it from the cloud? It goes to the Drive "
                      "trash.\n\nYes: delete it.\nNo: keep it.") != Answer::Yes)
    {
        return RemoveResult::Kept;
    }
    if (!m_cloud.TrashFile(save.fileId))
    {
        return RemoveResult::Failed;
    }
    debugPrintf("SaveBrowser::Remove: %s\n", name.c_str());
    m_state.Forget(name);
    m_saves.erase(name);
    return RemoveResult::Removed;
}
