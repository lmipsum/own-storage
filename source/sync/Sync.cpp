// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Sync.h"

#include "app/DebugLog.h"
#include "sync/GameName.h"

#include <set>
#include <sstream>
#include <utility>

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
           const std::string &workDirectory, const SaveTarget *target)
    : m_cloud(cloud), m_state(state), m_storage(storage), m_display(display),
      m_scanner(storage, savedata, ownSave),
      m_archives(cloud, state, storage, display, savedata, ownSave, workDirectory, target),
      m_rootFolderId(rootFolderId), m_ownSave(ownSave)
{
}

bool Sync::Run()
{
    m_display.SetText(ProgressBar::Overall, "Comparing saves...");
    std::map<std::string, CloudSave> cloudSaves;
    if (!ListCloudSaves(m_cloud, m_rootFolderId, m_ownSave, m_state, cloudSaves))
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

const std::vector<std::string> &Sync::NotResigned() const
{
    return m_notResigned;
}

void Sync::Decide(const std::string &name, const LocalSave *local, const CloudSave *cloud,
                  Plan &plan)
{
    const SaveBase *base = m_state.Find(name);
    // The cloud's archive is another file than the base knew, such as one of another account
    // after a sign-out, or one another console made anew: the base says nothing about it.
    // A download cut short is finished from the cloud's copy whichever file that is now.
    if (base != nullptr && !base->downloading && cloud != nullptr && base->fileId != cloud->fileId)
    {
        base = nullptr;
    }
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
            // A changed fingerprint may still mean the same content; the archive tells. It is
            // written again when its turn to go up comes, so archives are not all on the disk at
            // the same time.
            const std::string md5 = m_archives.ArchiveMd5(*local);
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
        m_storage.Remove(m_archives.ArchivePath(upload.save->name));
    }
    // A full cloud stops only uploads; bringing saves down needs no room there.
    for (const Download &download : plan.downloads)
    {
        if (m_display.Cancelled())
        {
            break;
        }
        const BroughtDown result = m_archives.BringDown(download.name, download.cloud);
        if (result != BroughtDown::Failed)
        {
            progress.Advance();
        }
        if (result == BroughtDown::DoneNotResigned)
        {
            m_notResigned.push_back(download.name);
        }
    }
}

Sync::Plan Sync::Settle(const std::vector<Question> &questions)
{
    // One question per game and doubt; the answer goes for every save of the game it covers.
    std::map<std::pair<Doubt, std::string>, std::vector<const Question *>> games;
    for (const Question &question : questions)
    {
        games[std::make_pair(question.doubt, TitleId(question.name))].push_back(&question);
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
            // A save deleted on the console that the user does not want back stays in the cloud
            // with its base, so the next sync asks again.
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
            title = question->cloud.title;
        }
        else if (question->local != nullptr)
        {
            title = m_archives.LocalTitle(question->name);
        }
        if (!title.empty())
        {
            break;
        }
    }
    const bool one = questions.size() == 1;
    const char *copies = one ? "copy replaces" : "copies replace";
    const char *them = one ? "it" : "them";
    std::ostringstream text;
    text << GameLabel(titleId, title) << ":\n";
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
             << "No: leave " << them << " in the cloud; the next sync asks again.";
    }
    return text.str();
}

void Sync::QueueUpload(const LocalSave &save, const std::string &fileId, Progress &progress)
{
    const std::string path = m_archives.ArchivePath(save.name);
    m_cloud.QueueFile(
        path,
        [this, &save, fileId, path](FileToStore &file)
        {
            if (m_archives.WriteArchive(save, path).empty())
            {
                return false;
            }
            file.title = ArchiveTitle(save.name);
            // Every console can then name the game without bringing the archive down.
            file.description = m_archives.LocalTitle(save.name);
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

bool Sync::Stopped() const
{
    return m_display.Cancelled() || m_cloud.IsFull();
}
