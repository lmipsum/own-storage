// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Sync.h"

#include "app/DebugLog.h"
#include "sync/ParamSfo.h"
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
// A save folder's name starts with the ID of its game.
const std::size_t kTitleIdLength = 9;
// The longest title a PARAM.SFO holds; a title from the cloud may be anything, and the system
// dialog refuses a text that is too long.
const std::size_t kMaxTitleLength = 128;

/**
 * A title as the question shows it: no longer than a PARAM.SFO's, cut where a UTF-8 character
 * starts, and without control characters.
 */
std::string ShownTitle(const std::string &title)
{
    std::string shown;
    for (const char c : title)
    {
        if (static_cast<unsigned char>(c) >= ' ' && c != '\x7f')
        {
            shown += c;
        }
    }
    if (shown.size() <= kMaxTitleLength)
    {
        return shown;
    }
    std::size_t end = kMaxTitleLength;
    // Bytes 10xxxxxx continue a character; the cut goes before the byte that starts it.
    while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80)
    {
        --end;
    }
    return shown.substr(0, end);
}

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

    Plan plan;
    for (const std::string &name : names)
    {
        const auto local = localSaves.find(name);
        const auto cloud = cloudSaves.find(name);
        Decide(name, local == localSaves.end() ? nullptr : &local->second,
               cloud == cloudSaves.end() ? nullptr : &cloud->second, plan);
    }
    Transfer(plan);
    // The questions come once everything that needs none is done.
    if (!plan.questions.empty() && !Stopped())
    {
        Transfer(Settle(plan.questions));
    }
    return true;
}

void Sync::Decide(const std::string &name, const LocalSave *local, const CloudSave *cloud,
                  Plan &plan)
{
    const SaveBase *base = m_state.Find(name);
    if (local == nullptr && cloud == nullptr)
    {
        m_state.Forget(name);
    }
    else if (base != nullptr && base->downloading)
    {
        // An earlier run ended while writing the save; the cloud's copy is the one to have.
        if (cloud != nullptr)
        {
            plan.downloads.push_back(Download{name, *cloud});
        }
        else
        {
            debugPrintf("Sync::Decide: %s was left half written and is gone from the cloud\n",
                        name.c_str());
        }
    }
    else if (cloud == nullptr)
    {
        // New on the console, or gone from the cloud while the console still has it.
        plan.uploads.push_back(Upload{local, std::string()});
    }
    else if (local == nullptr)
    {
        if (base == nullptr)
        {
            plan.downloads.push_back(Download{name, *cloud});
        }
        else
        {
            const Doubt doubt = cloud->md5 == base->md5 ? Doubt::DeletedOnConsole
                                                        : Doubt::DeletedOnConsoleChangedInCloud;
            plan.questions.push_back(Question{doubt, name, *cloud, nullptr});
        }
    }
    else
    {
        const bool localChanged = base == nullptr || local->fingerprint != base->fingerprint;
        const bool cloudChanged = base == nullptr || cloud->md5 != base->md5;
        if (!localChanged && cloudChanged)
        {
            plan.downloads.push_back(Download{name, *cloud});
        }
        else if (localChanged)
        {
            // A changed fingerprint may still mean the same content; the archive tells.
            // It is removed at once and written again when its turn to go up comes, so
            // archives are not all on the disk at the same time.
            const std::string path = ArchivePath(name);
            const std::string md5 = WriteArchive(*local, path);
            m_storage.Remove(path);
            if (!md5.empty() && md5 == cloud->md5)
            {
                m_state.Record(name, SaveBase{cloud->fileId, md5, local->fingerprint, false});
            }
            else if (!md5.empty() && !cloudChanged)
            {
                plan.uploads.push_back(Upload{local, cloud->fileId});
            }
            else if (!md5.empty())
            {
                plan.questions.push_back(Question{Doubt::ChangedOnBothSides, name, *cloud, local});
            }
        }
    }
}

void Sync::Transfer(const Plan &plan)
{
    Progress progress(m_display, plan.uploads.size() + plan.downloads.size());
    debugPrintf("Sync::Transfer: %u saves to send, %u to bring down\n",
                static_cast<unsigned int>(plan.uploads.size()),
                static_cast<unsigned int>(plan.downloads.size()));
    for (const Upload &upload : plan.uploads)
    {
        QueueUpload(*upload.save, upload.fileId, progress);
    }
    m_cloud.RunQueue([this] { return Stopped(); });
    // A stopped queue leaves the archives it had written behind.
    for (const Upload &upload : plan.uploads)
    {
        m_storage.Remove(ArchivePath(upload.save->name));
    }
    // A full cloud stops only uploads; bringing saves down needs no room there.
    for (const Download &download : plan.downloads)
    {
        if (m_display.Cancelled())
        {
            break;
        }
        if (BringDown(download.name, download.cloud))
        {
            progress.Advance();
        }
    }
}

Sync::Plan Sync::Settle(const std::vector<Question> &questions)
{
    // One question per game and doubt; the answer goes for every save of the game it covers.
    std::map<std::pair<Doubt, std::string>, std::vector<const Question *>> games;
    for (const Question &question : questions)
    {
        games[std::make_pair(question.doubt, question.name.substr(0, kTitleIdLength))].push_back(
            &question);
    }
    Plan plan;
    for (const auto &game : games)
    {
        const Answer answer =
            m_display.Ask(QuestionText(game.first.first, game.first.second, game.second));
        if (answer == Answer::None)
        {
            break;
        }
        for (const Question *question : game.second)
        {
            debugPrintf("Sync::Settle: %s answered %s\n", question->name.c_str(),
                        answer == Answer::Yes ? "yes" : "no");
            if (answer == Answer::Yes)
            {
                plan.downloads.push_back(Download{question->name, question->cloud});
            }
            else if (question->doubt == Doubt::ChangedOnBothSides)
            {
                plan.uploads.push_back(Upload{question->local, question->cloud.fileId});
            }
            else if (m_cloud.TrashFile(question->cloud.fileId))
            {
                m_state.Forget(question->name);
            }
        }
    }
    return plan;
}

std::string Sync::QuestionText(Doubt doubt, const std::string &titleId,
                               const std::vector<const Question *> &questions) const
{
    std::string title;
    for (const Question *question : questions)
    {
        if (!question->cloud.title.empty())
        {
            title = ShownTitle(question->cloud.title);
        }
        else if (question->local != nullptr)
        {
            title = ShownTitle(LocalTitle(question->name));
        }
        if (!title.empty())
        {
            break;
        }
    }
    const std::string game = title.empty() ? titleId : title + " (" + titleId + ")";
    const bool one = questions.size() == 1;
    const char *copies = one ? "copy replaces" : "copies replace";
    const char *them = one ? "it" : "them";
    std::ostringstream text;
    text << game << ":\n";
    if (one)
    {
        text << "a save";
    }
    else
    {
        text << questions.size() << " saves";
    }
    if (doubt == Doubt::ChangedOnBothSides)
    {
        text << (one ? " has" : " have") << " changed both on this PS3 and in the cloud.\n\n"
             << "Yes: the cloud's " << copies << " this PS3's.\n"
             << "No: this PS3's " << copies << " the cloud's.";
    }
    else
    {
        text << (one ? " was" : " were") << " deleted on this PS3";
        if (doubt == Doubt::DeletedOnConsole)
        {
            text << " but " << (one ? "is" : "are") << " still in the cloud.\n\n";
        }
        else
        {
            text << ", but another PS3 has sent " << (one ? "a newer copy of it" : "newer copies")
                 << " to the cloud since.\n\n";
        }
        text << "Yes: bring " << them << " back to this PS3.\n"
             << "No: delete " << them << " from the cloud too.";
    }
    return text.str();
}

std::string Sync::LocalTitle(const std::string &name) const
{
    std::string sfo;
    return m_storage.Read(m_savedata + "/" + name + "/" + paramSfoName, sfo) ? ReadSfoTitle(sfo)
                                                                             : std::string();
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
        const CloudSave save{file["id"].Str(), file["md5Checksum"].Str(),
                             file["description"].Str()};
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
            // Every console can then name the game without bringing the archive down.
            file.description = LocalTitle(save.name);
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

bool Sync::BringDown(const std::string &name, const CloudSave &cloudSave)
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
        debugPrintf("Sync::BringDown: %s could not be brought down whole\n", name.c_str());
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
            debugPrintf("Sync::BringDown: %s/%s cannot be written\n", name.c_str(),
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
    debugPrintf("Sync::BringDown: %s saved\n", name.c_str());
    return true;
}

bool Sync::Stopped() const
{
    return m_display.Cancelled() || m_cloud.IsFull();
}
