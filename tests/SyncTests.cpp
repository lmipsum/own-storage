// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"
#include "SaveFiles.h"
#include "TestCloud.h"
#include "TestConsole.h"

#include "app/CloudSaveText.h"
#include "app/LauncherSetup.h"
#include "app/Notice.h"
#include "app/XmbLists.h"
#include "app/XmbPick.h"
#include "sync/Clock.h"
#include "sync/CloudSaves.h"
#include "sync/GameName.h"
#include "sync/ParamPfd.h"
#include "sync/ParamSfo.h"
#include "sync/SaveBrowser.h"
#include "sync/SaveImport.h"
#include "sync/SaveProgress.h"
#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"
#include "sync/Tar.h"

#include <mbedtls/md.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{
/** A display that keeps what the bars said, never asks to stop and answers from a script. */
class ScriptedDisplay : public SyncDisplay
{
  public:
    void SetText(ProgressBar bar, const std::string &text) override
    {
        (bar == ProgressBar::Overall ? overallTexts : currentTexts).push_back(text);
    }
    void Reset(ProgressBar) override {}
    void Advance(ProgressBar, unsigned int) override {}
    bool Cancelled() const override
    {
        return false;
    }
    Answer Ask(const std::string &question) override
    {
        questions.push_back(question);
        if (answers.empty())
        {
            return Answer::None;
        }
        const Answer answer = answers.front();
        answers.erase(answers.begin());
        return answer;
    }

    /** Given in turn; once they run out no answer comes, as when the app closes. */
    std::vector<Answer> answers;
    /** A question asked; empty when there were fewer, so a check fails instead of crashing. */
    std::string Question(std::size_t index) const
    {
        return index < questions.size() ? questions[index] : std::string();
    }

    /** Every question asked. */
    std::vector<std::string> questions;
    /** Every text of each bar, in turn. */
    std::vector<std::string> overallTexts;
    std::vector<std::string> currentTexts;
};

/** A clock the test moves by hand. */
class HandClock : public Clock
{
  public:
    std::time_t Seconds() const override
    {
        return now;
    }

    std::time_t now = 0;
};

/** Whether a bar said the text at some time. */
bool Shown(const std::vector<std::string> &texts, const std::string &text)
{
    return std::find(texts.begin(), texts.end(), text) != texts.end();
}

// Saves written before the first sync.
const std::time_t kWritten = std::time(nullptr) - 3600;
// A later change on the console.
const std::time_t kChanged = kWritten + 60;

// The app's own save in the logged-in user's savedata.
const char kOwnSave[] = "OWNS00001-PROFILE";
const char kSavedata[] = "/dev_hdd0/home/00000001/savedata";
const char kSave[] = "/dev_hdd0/home/00000001/savedata/BLES00001-SAVE";

/** A time as Drive gives modifiedDate. */
std::string CloudTime(std::time_t time)
{
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.000Z", std::gmtime(&time));
    return text;
}

/**
 * Fills a console with the logged-in user's saves and the app's own save, another user's save
 * of the same name, and a memory card.
 */
void PutSaves(TestConsole &console)
{
    console.Put(std::string(kSave) + "/PARAM.SFO", MakeSfo("Game One"), kWritten);
    console.Put(std::string(kSave) + "/DATA.BIN", "data-1", kWritten);
    console.Put("/dev_hdd0/home/00000001/savedata/BLUS00002-AUTO/DATA.BIN", "data-2", kWritten);
    console.Put("/dev_hdd0/home/00000001/savedata/OWNS00001-PROFILE/MARKER.DAT", "1", kWritten);
    console.Put("/dev_hdd0/home/00000002/savedata/BLES00001-SAVE/DATA.BIN", "other user", kWritten);
    console.Put("/dev_hdd0/savedata/vmc/CARD1.VM2", "card", kWritten);
}

/** What the first sync of PutSaves() leaves in the Own Storage folder. */
const char kFirstSyncTree[] = "BLES00001-SAVE.tar\n"
                              "BLUS00002-AUTO.tar\n";

/** A cloud and the logged-in user's console, as runs of the app see them. */
struct Setup
{
    TestConsole console;
    TestCloud cloud;
    ScriptedDisplay display;
    HandClock clock;
    std::string rootId;
    /** Whom saves downloaded are made to belong to; null to leave them as they are. */
    const SaveTarget *target = nullptr;

    Setup()
    {
        rootId = cloud.CreateFolder(driveFolderName, "root")["id"].Str();
    }

    /** One run of the app on a console, with the state that console kept. */
    bool Run(TestConsole &on, const std::string &savedata)
    {
        SyncState state(on.Scratch() + "/state.json", on.Scratch() + "/state.backup");
        state.Load();
        return Sync(cloud, state, on, display, clock, rootId, savedata, kOwnSave, on.Scratch(),
                    target)
            .Run();
    }

    /** One run of the app on the logged-in user's console. */
    bool Run()
    {
        return Run(console, kSavedata);
    }

    /** The cloud id of a save's archive; empty when there is none. */
    std::string ArchiveId(const std::string &save) const
    {
        for (const auto &item : cloud.Items())
        {
            if (item.second.title == save + ".tar")
            {
                return item.first;
            }
        }
        return std::string();
    }

    /**
     * Plays the save on another console of the same user, then on this one, so that it changed
     * on both sides since its base.
     */
    void ChangeOnBothSides()
    {
        TestConsole other;
        other.MakeDirectories(kSavedata);
        Run(other, kSavedata);
        other.Put(std::string(kSave) + "/DATA.BIN", "played there", kChanged);
        Run(other, kSavedata);
        console.Put(std::string(kSave) + "/DATA.BIN", "played here", kChanged);
    }

    /** Deletes a save on this console. */
    void DeleteSave(const std::string &save)
    {
        const std::string folder = std::string(kSavedata) + "/" + save;
        for (const std::string &file : console.List(folder))
        {
            console.Remove(folder + "/" + file);
        }
        console.RemoveDirectory(folder);
    }

    /** The files in the cloud's archive of a save. */
    std::vector<TarFile> Archive(const std::string &save) const
    {
        std::vector<TarFile> files;
        for (const auto &item : cloud.Items())
        {
            if (item.second.title == save + ".tar")
            {
                ReadTar(item.second.content, files);
            }
        }
        return files;
    }
};
/** The cloud's saves as one console of the user sees them, to download or delete one by one. */
struct Browsing
{
    Browsing(Setup &setup, TestConsole &on)
        : state(on.Scratch() + "/state.json", on.Scratch() + "/state.backup"),
          browser(setup.cloud, state, on, setup.display, setup.rootId, kSavedata, kOwnSave,
                  on.Scratch(), setup.target)
    {
        state.Load();
        browser.Refresh();
    }

    SyncState state;
    SaveBrowser browser;
};
} // namespace

TEST(ArchiveKeepsTheBytesOfTheArchivesUploadedBefore)
{
    Setup setup;
    setup.console.Put(std::string(kSavedata) + "/BLES00009-GOLD/B.BIN", "bb", kWritten);
    setup.console.Put(std::string(kSavedata) + "/BLES00009-GOLD/A.BIN", "a", kWritten);
    setup.Run();
    // The MD5 an archive of these files had before archives were written a file at a time.
    const std::string archive = setup.cloud.Items().at(setup.ArchiveId("BLES00009-GOLD")).content;
    unsigned char digest[16];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5),
               reinterpret_cast<const unsigned char *>(archive.data()), archive.size(), digest);
    std::string md5;
    for (const unsigned char byte : digest)
    {
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02x", byte);
        md5 += hex;
    }
    CHECK_EQUAL(std::string("7ac9469dbf90f4831dc4e29f4bb1cc57"), md5);
}

TEST(LargeSaveIsUploadedAndDownloadedAPieceAtATime)
{
    Setup setup;
    PutSaves(setup.console);
    // Larger than the pieces a large file moves in, and not a whole number of them.
    std::string big(2 * 1024 * 1024 + 700, '\0');
    for (std::size_t i = 0; i < big.size(); ++i)
    {
        big[i] = static_cast<char>(i % 251);
    }
    setup.console.Put(std::string(kSave) + "/BIG.DAT", big, kWritten);
    setup.Run();
    // The archive is the one a save always gave, so its MD5 matches the archives uploaded before.
    std::string data;
    std::string sfo;
    setup.console.Read(std::string(kSave) + "/DATA.BIN", data);
    setup.console.Read(std::string(kSave) + "/PARAM.SFO", sfo);
    CHECK(setup.cloud.Items().at(setup.ArchiveId("BLES00001-SAVE")).content ==
          MakeTar({{"BIG.DAT", big}, {"DATA.BIN", data}, {"PARAM.SFO", sfo}}));
    TestConsole other;
    other.MakeDirectories(kSavedata);
    setup.Run(other, kSavedata);
    std::string downloaded;
    CHECK(other.Read(std::string(kSave) + "/BIG.DAT", downloaded));
    CHECK(downloaded == big);
}

TEST(ArchiveIsTheSameWhateverTheOrderAndReadsBack)
{
    const std::vector<TarFile> files = {{"PARAM.SFO", "sfo"}, {"DATA.BIN", std::string(700, 'x')}};
    const std::string archive = MakeTar(files);
    CHECK_EQUAL(archive, MakeTar({files[1], files[0]}));
    std::vector<TarFile> read;
    CHECK(ReadTar(archive, read));
    CHECK_EQUAL(std::size_t(2), read.size());
    CHECK_EQUAL(std::string("DATA.BIN"), read[0].name);
    CHECK_EQUAL(std::string(700, 'x'), read[0].content);
    CHECK_EQUAL(std::string("sfo"), read[1].content);
}

TEST(ArchiveThatLeavesTheSaveOrIsCutShortIsRefused)
{
    std::vector<TarFile> read;
    CHECK(!ReadTar(MakeTar({{"..", "x"}}), read));
    CHECK(!ReadTar(MakeTar({{"DIR/FILE", "x"}}), read));
    const std::string archive = MakeTar({{"DATA.BIN", "data"}});
    CHECK(!ReadTar(archive.substr(0, archive.size() - 1024), read));
    std::string damaged = archive;
    damaged[0] = 'X';
    CHECK(!ReadTar(damaged, read));
}

TEST(FirstSyncUploadsOneArchivePerSaveOfTheUserAlone)
{
    Setup setup;
    PutSaves(setup.console);
    CHECK(setup.Run());
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    const std::vector<TarFile> files = setup.Archive("BLES00001-SAVE");
    CHECK_EQUAL(std::size_t(2), files.size());
    CHECK_EQUAL(std::string("data-1"), files[0].content);
}

TEST(SyncShowsTheGameOfEachSaveItMoves)
{
    Setup setup;
    PutSaves(setup.console);
    CHECK(setup.Run(setup.console, kSavedata));
    // A save without its PARAM.SFO's title is named by its game's ID.
    CHECK(Shown(setup.display.currentTexts, "Uploading Game One"));
    CHECK(Shown(setup.display.currentTexts, "Uploading BLUS00002"));
    CHECK(Shown(setup.display.overallTexts, "Saves: 2 of 2"));
    TestConsole other;
    CHECK(setup.Run(other, kSavedata));
    CHECK(Shown(setup.display.currentTexts, "Downloading Game One"));
}

TEST(TimeLeftIsToldBySizeOnceTheSavesDoneTellEnough)
{
    const unsigned long long megabyte = 1024 * 1024;
    ScriptedDisplay display;
    HandClock clock;
    // Each save weighs its size and a megabyte more: 4 saves of 11 MB weigh 15 MB together.
    SaveProgress progress(display, clock, 4, 11 * megabyte);
    clock.now = 5;
    progress.Advance(megabyte);
    CHECK_EQUAL(std::string("Saves: 1 of 4"), display.overallTexts.back());
    // 4 MB took 24 s, so the 11 MB left take 66 s.
    clock.now = 24;
    progress.Advance(megabyte);
    CHECK_EQUAL(std::string("Saves: 2 of 4, about 1 min left"), display.overallTexts.back());
    // 6 MB took 60 s, so the last save's 9 MB take 90 s; nothing is left after it.
    clock.now = 60;
    progress.Advance(megabyte);
    CHECK_EQUAL(std::string("Saves: 3 of 4, about 2 min left"), display.overallTexts.back());
    clock.now = 150;
    progress.Advance(8 * megabyte);
    CHECK_EQUAL(std::string("Saves: 4 of 4"), display.overallTexts.back());
    CHECK_EQUAL(std::string("less than a minute left"), SaveProgress::TimeLeft(59));
    CHECK_EQUAL(std::string("about 1 min left"), SaveProgress::TimeLeft(89));
    CHECK_EQUAL(std::string("about 2 min left"), SaveProgress::TimeLeft(90));
}

TEST(UnchangedRerunUploadsNothing)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    const int creates = setup.cloud.Creates();
    setup.Run();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(ChangedSaveReplacesItsArchive)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    const int creates = setup.cloud.Creates();
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "data-1 changed", kChanged);
    setup.Run();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(1, setup.cloud.Updates());
    CHECK_EQUAL(std::string("data-1 changed"), setup.Archive("BLES00001-SAVE")[0].content);
}

TEST(UploadedSaveIsDatedByItsNewestFile)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "data-1", kChanged);
    setup.Run();
    CHECK_EQUAL(CloudTime(kChanged), setup.cloud.Modified(setup.ArchiveId("BLES00001-SAVE")));
    // Now the file that sorts last is the newest.
    setup.console.Put(std::string(kSave) + "/PARAM.SFO", MakeSfo("Game One v2"), kChanged + 60);
    setup.Run();
    CHECK_EQUAL(CloudTime(kChanged + 60), setup.cloud.Modified(setup.ArchiveId("BLES00001-SAVE")));
}

TEST(TouchedButSameSaveUploadsNothing)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "data-1", kChanged);
    setup.Run();
    setup.Run();
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(CreateWithLostAnswerIsTakenOverNextRun)
{
    Setup setup;
    PutSaves(setup.console);
    setup.cloud.LoseAnswerOfCreate(0);
    setup.Run();
    const int creates = setup.cloud.Creates();
    setup.Run();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(0, setup.cloud.Updates());
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
}

TEST(NewConsoleGetsTheBackupUnderAnotherUserNumber)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    // The same person is user 2 there; the console made that user's savedata directory.
    other.Put("/dev_hdd0/home/00000002/localusername", "two", kWritten);
    other.MakeDirectory("/dev_hdd0/home/00000002/savedata");
    CHECK(setup.Run(other, "/dev_hdd0/home/00000002/savedata"));
    CHECK_EQUAL(std::string("data-1"),
                other.Content("/dev_hdd0/home/00000002/savedata/BLES00001-SAVE/DATA.BIN"));
    CHECK_EQUAL(std::string("data-2"),
                other.Content("/dev_hdd0/home/00000002/savedata/BLUS00002-AUTO/DATA.BIN"));
}

TEST(SaveChangedOnAnotherConsoleIsDownloaded)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories("/dev_hdd0/home/00000001/savedata");
    setup.Run(other, kSavedata);
    other.Put(std::string(kSave) + "/DATA.BIN", "played on", kChanged);
    other.Remove(std::string(kSave) + "/PARAM.SFO");
    setup.Run(other, kSavedata);
    setup.Run();
    CHECK_EQUAL(std::string("played on"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK(!setup.console.Exists(std::string(kSave) + "/PARAM.SFO"));
    // What was downloaded is the base now: the next run has nothing to do.
    const int updates = setup.cloud.Updates();
    setup.Run();
    CHECK_EQUAL(updates, setup.cloud.Updates());
}

TEST(SaveChangedOnBothSidesIsLeftAsItIsWithoutAnAnswer)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.ChangeOnBothSides();
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("Game One (BLES00001):\n"));
    CHECK_EQUAL(std::string("played here"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(std::string("played there"), setup.Archive("BLES00001-SAVE")[0].content);
}

TEST(SaveChangedOnBothSidesIsDownloadedOnYes)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.ChangeOnBothSides();
    setup.display.answers = {Answer::Yes};
    setup.Run();
    CHECK_EQUAL(std::string("played there"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
}

TEST(SaveChangedOnBothSidesIsLeftAsItIsWhenTheSecondQuestionGoesUnanswered)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.ChangeOnBothSides();
    // No to the first; the app closes before the second is answered.
    setup.display.answers = {Answer::No};
    setup.Run();
    CHECK_EQUAL(std::size_t(2), setup.display.questions.size());
    CHECK_EQUAL(std::string("played there"), setup.Archive("BLES00001-SAVE")[0].content);
    CHECK_EQUAL(std::string("played here"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
}

TEST(SaveChangedOnBothSidesIsUploadedOnlyWhenAskedToo)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.ChangeOnBothSides();
    // Neither way: both copies stay, and the next sync asks again.
    setup.display.answers = {Answer::No, Answer::No};
    setup.Run();
    CHECK_EQUAL(std::string("played there"), setup.Archive("BLES00001-SAVE")[0].content);
    CHECK_EQUAL(std::string("played here"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK(setup.display.Question(1).find("Replace the cloud's copy with this PS3's?") !=
          std::string::npos);
    // Not the cloud's, but this PS3's: it is uploaded.
    setup.display.answers = {Answer::No, Answer::Yes};
    setup.Run();
    CHECK_EQUAL(std::string("played here"), setup.Archive("BLES00001-SAVE")[0].content);
    setup.Run();
    CHECK_EQUAL(std::size_t(4), setup.display.questions.size());
}

TEST(SaveDeletedOnTheConsoleIsNotDownloadedAgainWithoutAnAnswer)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.DeleteSave("BLES00001-SAVE");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK(!setup.console.Exists(kSave));
    CHECK_EQUAL(std::size_t(2), setup.Archive("BLES00001-SAVE").size());
}

TEST(SaveDeletedOnTheConsoleComesBackOnYes)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.DeleteSave("BLES00001-SAVE");
    setup.display.answers = {Answer::Yes};
    setup.Run();
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
}

TEST(SaveDeletedOnTheConsoleStaysInTheCloudOnNoAndIsAskedAgain)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.DeleteSave("BLES00001-SAVE");
    setup.display.answers = {Answer::No};
    setup.Run();
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    CHECK(!setup.console.Exists(kSave));
    setup.Run();
    CHECK_EQUAL(std::size_t(2), setup.display.questions.size());
    CHECK(!setup.console.Exists(kSave));
}

TEST(SaveDeletedHereButPlayedOnElsewhereSaysSo)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    setup.Run(other, kSavedata);
    other.Put(std::string(kSave) + "/DATA.BIN", "played there", kChanged);
    setup.Run(other, kSavedata);
    setup.DeleteSave("BLES00001-SAVE");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK(setup.display.Question(0).find("another PS3 has uploaded a newer copy") !=
          std::string::npos);
}

TEST(LongTitleFromTheCloudIsCut)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    const std::string id = setup.ArchiveId("BLES00001-SAVE");
    // A title of 129 bytes whose last character, two bytes long, would be cut in half.
    const std::string title = std::string(127, 'a') + "\xc3\xa9";
    setup.cloud.SetDescription(id, title);
    setup.DeleteSave("BLES00001-SAVE");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0),
                setup.display.Question(0).find(std::string(127, 'a') + " (BLES00001):\n"));
}

TEST(SavesOfOneGameAreAskedAboutOnce)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN", "data-3", kWritten);
    setup.Run();
    setup.DeleteSave("BLES00001-SAVE");
    setup.DeleteSave("BLES00001-SAVE2");
    setup.display.answers = {Answer::Yes};
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK(setup.display.Question(0).find("2 saves were deleted") != std::string::npos);
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(std::string("data-3"),
                setup.console.Content(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN"));
}

TEST(NoAnswerEndsTheQuestions)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.DeleteSave("BLES00001-SAVE");
    setup.DeleteSave("BLUS00002-AUTO");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
}

TEST(GameWithoutATitleIsNamedByItsId)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.DeleteSave("BLUS00002-AUTO");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("BLUS00002:\n"));
}

TEST(ArchiveCarriesTheGameTitleForEveryConsole)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    CHECK_EQUAL(std::string("Game One\n\n"),
                setup.cloud.Description(setup.ArchiveId("BLES00001-SAVE")));
    // The title comes from the cloud once the console has no PARAM.SFO left to read.
    setup.DeleteSave("BLES00001-SAVE");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("Game One (BLES00001):\n"));
}

TEST(SaveTextsAreReadFromParamSfo)
{
    const SaveTexts texts =
        ReadSfoTexts(MakeSfo(SaveTexts{"Game One", "Chapter 3", "Level 12\nPlayed: 10:00"},
                             TestOwner(1, 'a', "0123456789abcdef")));
    CHECK_EQUAL(std::string("Game One"), texts.title);
    CHECK_EQUAL(std::string("Chapter 3"), texts.subtitle);
    CHECK_EQUAL(std::string("Level 12\nPlayed: 10:00"), texts.detail);
    CHECK_EQUAL(std::string(), ReadSfoTexts(MakeSfo("Game One")).detail);
}

TEST(DescriptionKeepsTheTextsLineByLine)
{
    const SaveTexts texts{"Game\nOne", "Chapter\n3", "Level 12\nPlayed: 10:00"};
    const std::string description = DescribeSave(texts);
    CHECK_EQUAL(std::string("Game One\nChapter 3\nLevel 12\nPlayed: 10:00"), description);
    const SaveTexts read = ReadDescription(description);
    CHECK_EQUAL(std::string("Game One"), read.title);
    CHECK_EQUAL(std::string("Chapter 3"), read.subtitle);
    CHECK_EQUAL(texts.detail, read.detail);
    // Empty texts keep their lines, which tells the description from an older one.
    CHECK_EQUAL(std::string("Game One\n\n"), DescribeSave(SaveTexts{"Game One", "", ""}));
    CHECK_EQUAL(std::string("Sub"),
                ReadDescription(DescribeSave(SaveTexts{"", "Sub", ""})).subtitle);
    CHECK_EQUAL(std::string("Ends\n"),
                ReadDescription(DescribeSave(SaveTexts{"T", "", "Ends\n"})).detail);
    CHECK_EQUAL(std::string("Game One"),
                ReadDescription(DescribeSave(SaveTexts{"Game\rOne", "", ""})).title);
    // An older description is the title alone, which may run over two lines.
    CHECK_EQUAL(std::string("Game One"), ReadDescription("Game One").title);
    const SaveTexts older = ReadDescription("Game\nOne");
    CHECK_EQUAL(std::string("Game One"), older.title);
    CHECK_EQUAL(std::string(), older.subtitle);
}

TEST(ArchiveCarriesTheSaveTextsForEveryConsole)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSave) + "/PARAM.SFO",
                      MakeSfo(SaveTexts{"Game One", "Chapter 3", "Level 12"},
                              TestOwner(1, 'a', "0123456789abcdef")),
                      kWritten);
    setup.Run();
    CHECK_EQUAL(std::string("Game One\nChapter 3\nLevel 12"),
                setup.cloud.Description(setup.ArchiveId("BLES00001-SAVE")));
}

TEST(TextIsCutWhereACharacterStarts)
{
    CHECK_EQUAL(std::string("abc"), CutText("abc", 5));
    // "é" is two bytes; a cut in its middle leaves it out whole.
    CHECK_EQUAL(std::string("a"), CutText("a\xc3\xa9", 2));
    CHECK_EQUAL(std::string("a\xc3\xa9"), CutText("a\xc3\xa9", 3));
}

TEST(GameTitleIsReadFromParamSfo)
{
    const std::string sfo = MakeSfo("Game One");
    CHECK_EQUAL(std::string("Game One"), ReadSfoTitle(sfo));
    CHECK_EQUAL(std::string(), ReadSfoTitle(sfo.substr(0, sfo.size() - 4)));
    CHECK_EQUAL(std::string(), ReadSfoTitle("sfo-1"));
}

TEST(FailedListingStopsTheSync)
{
    Setup setup;
    PutSaves(setup.console);
    setup.cloud.FailSearches(true);
    CHECK(!setup.Run());
    CHECK_EQUAL(1, setup.cloud.Creates());
}

TEST(SaveGoneFromTheCloudIsUploadedAgain)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    for (const auto &item : setup.cloud.Items())
    {
        if (item.second.title == "BLES00001-SAVE.tar")
        {
            setup.cloud.Delete(item.first);
            break;
        }
    }
    setup.Run();
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
}

TEST(SaveGoneOnBothSidesIsForgotten)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    const std::vector<TarFile> files = setup.Archive("BLES00001-SAVE");
    for (const auto &item : setup.cloud.Items())
    {
        if (item.second.title == "BLES00001-SAVE.tar")
        {
            setup.cloud.Delete(item.first);
            break;
        }
    }
    setup.console.Remove(std::string(kSave) + "/DATA.BIN");
    setup.console.Remove(std::string(kSave) + "/PARAM.SFO");
    setup.console.RemoveDirectory(kSave);
    setup.Run();
    // Without a base, an archive that turns up again is new to this console and is downloaded.
    setup.cloud.Store(TestCloudItem{"BLES00001-SAVE.tar", setup.rootId, octetMime, MakeTar(files)});
    setup.Run();
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
}

TEST(OfTwoArchivesOfASaveTheBaseKeepsItsOwn)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    std::string firstId;
    for (const auto &item : setup.cloud.Items())
    {
        if (item.second.title == "BLES00001-SAVE.tar")
        {
            firstId = item.first;
        }
    }
    setup.cloud.Delete(firstId);
    setup.Run();
    // Another console made an archive of the save under a lower id than this console's.
    setup.cloud.Store(firstId, TestCloudItem{"BLES00001-SAVE.tar", setup.rootId, octetMime,
                                             MakeTar({{"DATA.BIN", "played there"}})});
    setup.Run();
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(DamagedArchiveLeavesTheSaveAlone)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    const std::string id =
        setup.cloud.Store(TestCloudItem{"BLES00003-NEW.tar", setup.rootId, octetMime, "not a tar"});
    setup.Run();
    CHECK(!setup.console.Exists(std::string(kSavedata) + "/BLES00003-NEW"));
    // Without a base the next run tries again.
    setup.cloud.Store(id, TestCloudItem{"BLES00003-NEW.tar", setup.rootId, octetMime,
                                        MakeTar({{"DATA.BIN", "new"}})});
    setup.Run();
    CHECK_EQUAL(std::string("new"),
                setup.console.Content(std::string(kSavedata) + "/BLES00003-NEW/DATA.BIN"));
}

TEST(ArchiveDamagedOnTheWayLeavesTheSaveAlone)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    // A sound archive with bytes after its end still unpacks; only its MD5 tells.
    setup.cloud.Store(TestCloudItem{"BLES00003-NEW.tar", setup.rootId, octetMime,
                                    MakeTar({{"DATA.BIN", "new"}})});
    setup.cloud.DamageDownloads(true);
    setup.Run();
    CHECK(!setup.console.Exists(std::string(kSavedata) + "/BLES00003-NEW"));
}

TEST(OnlySaveArchivesAreDownloadedAndOfTwoTheFirst)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.cloud.Store(TestCloudItem{"BLES00001-SAVE.tar", setup.rootId, octetMime,
                                    MakeTar({{"DATA.BIN", "a copy"}})});
    setup.cloud.Store(TestCloudItem{"OWNS00001-PROFILE.tar", setup.rootId, octetMime,
                                    MakeTar({{"MARKER.DAT", "2"}})});
    setup.cloud.Store(
        TestCloudItem{"...tar", setup.rootId, octetMime, MakeTar({{"DATA.BIN", "outside"}})});
    setup.cloud.Store(TestCloudItem{"notes.txt", setup.rootId, octetMime, "notes"});
    TestConsole other;
    other.MakeDirectories(kSavedata);
    setup.Run(other, kSavedata);
    CHECK_EQUAL(std::string("data-1"), other.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK(!other.Exists(std::string(kSavedata) + "/OWNS00001-PROFILE"));
    CHECK(!other.Exists("/dev_hdd0/home/00000001/DATA.BIN"));
    CHECK(!other.Exists(std::string(kSavedata) + "/notes"));
}

TEST(DownloadCutShortIsDoneAgain)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    setup.Run(other, kSavedata);
    // The run ended after one file of the save was written.
    other.Put(std::string(kSave) + "/DATA.BIN", "half", kChanged);
    SyncState state(other.Scratch() + "/state.json", other.Scratch() + "/state.backup");
    state.Load();
    SaveBase base = *state.Find("BLES00001-SAVE");
    base.downloading = true;
    state.Record("BLES00001-SAVE", base);
    setup.Run(other, kSavedata);
    CHECK_EQUAL(std::string("data-1"), other.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(BrowserListsTheGamesWithTheirSaves)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN", "data-3", kWritten);
    setup.Run();
    Browsing browsing(setup, setup.console);
    const std::vector<CloudGame> games = browsing.browser.Games();
    CHECK_EQUAL(std::size_t(2), games.size());
    if (games.size() == 2)
    {
        // By name from A to Z: a game without a title goes by its ID.
        CHECK_EQUAL(std::string("BLUS00002"), games[0].label);
        CHECK_EQUAL(std::string("BLUS00002-AUTO"), games[0].saves.front());
        CHECK_EQUAL(std::string("Game One (BLES00001)"), games[1].label);
        CHECK_EQUAL(std::string("Game One"), games[1].name);
        CHECK_EQUAL(std::size_t(2), games[1].saves.size());
    }
    CHECK_EQUAL(setup.cloud.Items().at(setup.ArchiveId("BLES00001-SAVE")).content.size(),
                static_cast<std::size_t>(browsing.browser.Save("BLES00001-SAVE").size));
}

TEST(BrowserTellsHowEachSaveIsOnTheConsole)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    CHECK(Browsing(setup, other).browser.Status("BLES00001-SAVE") == SaveStatus::Missing);
    CHECK(Browsing(setup, setup.console).browser.Status("BLES00001-SAVE") == SaveStatus::Same);
    // Touched but the same content is still the cloud's copy.
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "data-1", kChanged);
    CHECK(Browsing(setup, setup.console).browser.Status("BLES00001-SAVE") == SaveStatus::Same);
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "played on", kChanged);
    CHECK(Browsing(setup, setup.console).browser.Status("BLES00001-SAVE") == SaveStatus::Different);
}

TEST(BrowserLoadsAMissingSaveWithoutAsking)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    Browsing browsing(setup, other);
    CHECK(browsing.browser.Load("BLES00001-SAVE") == LoadResult::Loaded);
    CHECK(setup.display.questions.empty());
    CHECK_EQUAL(std::string("data-1"), other.Content(std::string(kSave) + "/DATA.BIN"));
    // What was downloaded is the base, as after a sync.
    const SaveBase *base = browsing.state.Find("BLES00001-SAVE");
    CHECK(base != nullptr && base->fileId == setup.ArchiveId("BLES00001-SAVE") &&
          !base->downloading);
    CHECK(browsing.browser.Load("BLES00001-SAVE") == LoadResult::AlreadyThere);
}

TEST(BrowserAsksBeforeItReplacesADifferentSave)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "played here", kChanged);
    Browsing browsing(setup, setup.console);
    setup.display.answers = {Answer::No, Answer::Yes};
    CHECK(browsing.browser.Load("BLES00001-SAVE") == LoadResult::Kept);
    CHECK_EQUAL(std::string("played here"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK(browsing.browser.Load("BLES00001-SAVE") == LoadResult::Loaded);
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(std::size_t(2), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("Game One (BLES00001):\n"));
}

namespace
{
const XmbPlace kXmbPlace{"/dev_hdd0/xmlhost/game_plugin/", "/dev_hdd0/game/OWNS00001/USRDIR/xmb/",
                         "/dev_hdd0/game/OWNS00001/USRDIR/act.txt", "/dev_hdd0/tmp/launch"};
const DateStyle kXmbDates{DateOrder::DayMonthYear, false, 0};
const XmbView kNewestByTitle{XmbSort::DateNewest, XmbGroup::ByTitle};

/** A save as XmbGames() lists it, with what a test looks at; its description is its subtitle. */
XmbSave ListedSave(const std::string &name, const std::string &title, const std::string &subtitle,
                   bool downloaded, const std::string &modified = "2026-01-01T10:00:00.000Z")
{
    XmbSave save;
    save.name = name;
    save.title = title;
    save.subtitle = subtitle;
    save.info = subtitle;
    save.modified = modified;
    save.titleId = TitleId(name);
    save.downloaded = downloaded;
    return save;
}

/** A file of the lists; empty when they have none of the name. */
std::string ListFile(const XmbLists &lists, const std::string &name)
{
    for (const XmbFile &file : lists.files)
    {
        if (file.name == name)
        {
            return file.content;
        }
    }
    return std::string();
}
} // namespace

TEST(XmbListsTheSavesWithTheirIcons)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSave) + "/ICON0.PNG", "icon", kWritten);
    setup.Run();
    Browsing here(setup, setup.console);
    // By name: the game without a known title is named by its ID, which comes first.
    std::vector<XmbGame> games = XmbGames(here.browser, setup.console, kSavedata, kXmbDates);
    CHECK_EQUAL(std::size_t(2), games.size());
    if (games.size() == 2 && games[0].saves.size() == 1 && games[1].saves.size() == 1)
    {
        CHECK_EQUAL(std::string("BLUS00002"), games[0].title);
        CHECK_EQUAL(std::string("Game One"), games[1].title);
        CHECK_EQUAL(std::string("BLES00001"), games[1].titleId);
        // On the console without an icon of its own.
        const XmbSave &noIcon = games[0].saves[0];
        CHECK(noIcon.downloaded && noIcon.icon.empty());
        // On the console with its icon.
        const XmbSave &withIcon = games[1].saves[0];
        CHECK_EQUAL(std::string("Game One"), withIcon.title);
        // The game's ID is on its folder's row, not the save's.
        CHECK_EQUAL(0u, withIcon.info.find("BLES00001-SAVE\n"));
        CHECK(withIcon.info.find("BLES00001 |") == std::string::npos);
        CHECK(withIcon.downloaded);
        CHECK_EQUAL(std::string(kSave) + "/ICON0.PNG", withIcon.icon);
        // Without a subtitle of its own the save goes by its folder's name.
        const CloudSave &cloudSave = here.browser.Save("BLES00001-SAVE");
        CHECK_EQUAL(std::string("BLES00001-SAVE"), withIcon.subtitle);
        CHECK_EQUAL(ShownTime(cloudSave.modified, kXmbDates), withIcon.updated);
        CHECK_EQUAL(ShownSize(cloudSave.size), withIcon.size);
        CHECK_EQUAL(withIcon.subtitle + "\n" + withIcon.updated + "   " + withIcon.size,
                    withIcon.info);
        CHECK_EQUAL(cloudSave.modified, withIcon.modified);
        CHECK_EQUAL(std::string("BLES00001"), withIcon.titleId);
    }
    else
    {
        CHECK(false);
    }
    // A save the console has otherwise counts as not downloaded, so it shows no icon.
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "played on", kChanged);
    Browsing changed(setup, setup.console);
    games = XmbGames(changed.browser, setup.console, kSavedata, kXmbDates);
    CHECK(games.size() == 2 && games[1].saves.size() == 1 && !games[1].saves[0].downloaded &&
          games[1].saves[0].icon.empty());
}

TEST(XmbListsRowsThatStartTheAppThroughTheLauncher)
{
    const XmbLists signedOut = MakeXmbLists(kXmbPlace, false, true, "", {}, kNewestByTitle);
    const std::string signedOutMenu = ListFile(signedOut, xmbMenuFile);
    CHECK(signedOutMenu.find("<String>msg_signin</String>") != std::string::npos);
    CHECK(signedOutMenu.find("msg_sync_with_server") == std::string::npos);
    CHECK(signedOutMenu.find("msg_online_storage") == std::string::npos);
    CHECK(ListFile(signedOut, xmbRowsFile).empty());
    const XmbSave inCloud =
        ListedSave("BLES00001-SAVE", "Game One (BLES00001)", "Chapter \"3\" & more", false);
    const XmbSave downloaded = ListedSave("BLES00001-AUTO", "Game One (BLES00001)", "Auto", true);
    const XmbSave other = ListedSave("BLUS00002-SAVE", "Game Two", "Two", false);
    const XmbLists lists = MakeXmbLists(kXmbPlace, true, true, "3.2 GB / 15 GB",
                                        {XmbGame{"Game One", "BLES00001", {inCloud, downloaded}},
                                         XmbGame{"Game Two", "BLUS00002", {other}}},
                                        kNewestByTitle);
    const std::string folder = ListFile(lists, xmbFolderFile);
    const std::string menu = ListFile(lists, xmbMenuFile);
    const std::string saves = ListFile(lists, xmbSavesFile);
    const std::string games = ListFile(lists, xmbGamesFile);
    CHECK(folder.find("src=\"xmb://localhost/dev_hdd0/xmlhost/game_plugin/owns_menu.xml#") !=
          std::string::npos);
    CHECK(menu.find("<Pair key=\"title_rsc\"><String>msg_sync_with_server</String>") !=
          std::string::npos);
    CHECK(menu.find("msg_signout") != std::string::npos);
    CHECK(menu.find("msg_signin") == std::string::npos);
    // Only the USB row has a description of the app's own, and the saves in the cloud tell how full
    // the Google storage is; the others have none.
    CHECK(menu.find("Uploads the saves on a USB drive") != std::string::npos);
    const std::size_t firstInfo = menu.find("<Pair key=\"info\">");
    const std::size_t secondInfo = menu.find("<Pair key=\"info\">", firstInfo + 1);
    CHECK(secondInfo != std::string::npos &&
          menu.find("<Pair key=\"info\">", secondInfo + 1) == std::string::npos);
    CHECK(folder.find("<String>Cloud Save Utility</String>") != std::string::npos);
    CHECK(folder.find("<Pair key=\"info_rsc\"><String>msg_online_storage_explanation") !=
          std::string::npos);
    // The rows show the system's icons.
    CHECK(menu.find("<String>tex_loading_icon</String>") != std::string::npos);
    CHECK(menu.find("<String>item_tex_usb_connection</String>") != std::string::npos);
    CHECK(menu.find("<String>tex_Avatar_Default</String>") != std::string::npos);
    // The saves in the cloud come first, sign out last.
    CHECK(menu.find("msg_online_storage") < menu.find("msg_sync_with_server"));
    CHECK(menu.find("msg_usb") < menu.find("msg_signout"));
    // The saves in the cloud tell how full the Google storage is.
    CHECK(menu.find("<String>3.2 GB / 15 GB</String>") != std::string::npos);
    // The saves are in a folder of their own, in a file of their own, a folder per game, whose
    // saves are in yet another file.
    CHECK(menu.find("owns_saves.xml#") != std::string::npos);
    CHECK(menu.find("Game One") == std::string::npos);
    CHECK(saves.find("<String>Game One</String>") != std::string::npos);
    CHECK(saves.find("<String>BLES00001 | 2 saves</String>") != std::string::npos);
    CHECK(saves.find("<String>BLUS00002 | 1 save</String>") != std::string::npos);
    CHECK(saves.find("owns_games.xml#owns_game_1_view") != std::string::npos);
    CHECK(saves.find("BLES00001-SAVE") == std::string::npos);
    CHECK(games.find("<View id=\"owns_game_0_view\">") < games.find("Game One (BLES00001)"));
    CHECK(games.find("Game One (BLES00001)") < games.find("<View id=\"owns_game_1_view\">"));
    CHECK(games.find("<String>tex_album_icon</String>") != std::string::npos);
    CHECK(games.find("<String>item_tex_ps3util</String>") != std::string::npos);
    // The saves' keys run on from game to game.
    CHECK(games.find("key=\"owns_save_2\"") > games.find("owns_game_1_view"));
    // The texts are escaped, and the row runs its script, which leaves its action and starts the
    // app: webMAN does not write files for the XMB in its user mode, but its scripts do.
    CHECK(games.find("Chapter &quot;3&quot; &amp; more") != std::string::npos);
    CHECK(games.find("<String>/play.ps3/dev_hdd0/game/OWNS00001/USRDIR/xmb/owns_save_0.bat"
                     "</String>") != std::string::npos);
    bool saveScript = false;
    for (const XmbScript &script : lists.scripts)
    {
        saveScript =
            saveScript || (script.key == "owns_save_0" && script.action == "save:BLES00001-SAVE");
    }
    CHECK(saveScript);
    CHECK_EQUAL(std::string("del /dev_hdd0/game/OWNS00001/USRDIR/act.txt\n"
                            "logfile /dev_hdd0/game/OWNS00001/USRDIR/act.txt\n"
                            "log sync\ndel /dev_hdd0/tmp/launch\n"
                            "logfile /dev_hdd0/tmp/launch\nlog launch\n"),
                XmbScriptText(kXmbPlace, XmbScript{"owns_sync", xmbSync}));
    const std::string saveScriptText =
        XmbScriptText(kXmbPlace, XmbScript{"owns_save_0", "save:BLES00001-SAVE"});
    CHECK(saveScriptText.find("\nlog save:BLES00001-SAVE\n") != std::string::npos);
    CHECK(saveScriptText.find("\nlogfile /dev_hdd0/tmp/launch\nlog launch\n") != std::string::npos);
}

namespace
{
const LauncherPlace kLauncherPlace{"/dev_hdd0/game/OWNS00001/USRDIR/plugins/launcher.sprx",
                                   "/dev_hdd0/plugins/launcher.sprx", "/dev_hdd0/boot_plugins.txt",
                                   "/dev_hdd0/game/OWNS00001/USRDIR/boot_plugins.bak"};
} // namespace

TEST(BootListGetsThePluginLineOnceAndKeepsTheOthers)
{
    const std::string webman = "/dev_hdd0/plugins/webftp_server.sprx";
    CHECK_EQUAL(std::string("/p.sprx\r\n"), WithBootLine("", "/p.sprx"));
    // A last line without its end is ended before the plugin's goes after it.
    CHECK_EQUAL(webman + "\r\n/p.sprx\r\n", WithBootLine(webman, "/p.sprx"));
    CHECK_EQUAL(webman + "\n/p.sprx\r\n", WithBootLine(webman + "\n", "/p.sprx"));
    // A list that names the plugin already, whatever its line end, stays as it is.
    const std::string listed = "/p.sprx \r\n" + webman;
    CHECK_EQUAL(listed, WithBootLine(listed, "/p.sprx"));
    CHECK_EQUAL(webman + "\r\n/p.sprx.old\r\n/p.sprx\r\n",
                WithBootLine(webman + "\r\n/p.sprx.old\r\n", "/p.sprx"));
}

TEST(LauncherIsInstalledOnceAndAgainWhenTheAppBringsAnother)
{
    TestConsole console;
    const std::string webman = "/dev_hdd0/plugins/webftp_server.sprx\r\n";
    console.Put(kLauncherPlace.bundled, "plugin 1", kWritten);
    console.Put(kLauncherPlace.bootList, webman, kWritten);
    CHECK(SetUpLauncher(console, kLauncherPlace) == LauncherSetup::Changed);
    std::string content;
    CHECK(console.Read(kLauncherPlace.installed, content) && content == "plugin 1");
    CHECK(console.Read(kLauncherPlace.bootList, content) &&
          content == webman + "/dev_hdd0/plugins/launcher.sprx\r\n");
    // The list as it was is kept before the plugin's line is added.
    CHECK(console.Read(kLauncherPlace.bootListBackup, content) && content == webman);
    CHECK(SetUpLauncher(console, kLauncherPlace) == LauncherSetup::Unchanged);
    // An update brings another plugin; the backup stays the list from before the first.
    console.Put(kLauncherPlace.bundled, "plugin 2", kWritten);
    console.Put(kLauncherPlace.bootList, webman, kWritten);
    CHECK(SetUpLauncher(console, kLauncherPlace) == LauncherSetup::Changed);
    CHECK(console.Read(kLauncherPlace.installed, content) && content == "plugin 2");
    CHECK(console.Read(kLauncherPlace.bootListBackup, content) && content == webman);
}

TEST(LauncherOnAConsoleWithoutABootListStartsOne)
{
    TestConsole console;
    console.Put(kLauncherPlace.bundled, "plugin", kWritten);
    CHECK(SetUpLauncher(console, kLauncherPlace) == LauncherSetup::Changed);
    std::string content;
    CHECK(console.Read(kLauncherPlace.bootList, content) &&
          content == "/dev_hdd0/plugins/launcher.sprx\r\n");
    CHECK(!console.Exists(kLauncherPlace.bootListBackup));
    // An app built without the plugin leaves everything as it is.
    TestConsole without;
    CHECK(SetUpLauncher(without, kLauncherPlace) == LauncherSetup::Failed);
    CHECK(!without.Exists(kLauncherPlace.bootList));
}

TEST(NoticesTellResultsAndProblemsApart)
{
    // A result without the system's words is shown in the app's own, on one line.
    CHECK_EQUAL(std::string("The sync was stopped. Try again."),
                NotificationLine(Result("The sync was stopped.\nTry again.")));
    CHECK(!Result("Signed in.").problem);
    CHECK(Problem("Cannot reach Google.").problem);
    CHECK(ShownResult("BLES00001-SAVE", LoadResult::Failed).problem);
    CHECK(ShownResult("BLES00001-SAVE", RemoveResult::Failed).problem);
    CHECK_EQUAL(std::string("{explore_plugin:msg_deleted}"),
                NotificationLine(ShownResult("BLES00001-SAVE", RemoveResult::Removed)));
}

TEST(StorageUseReadsInNumbersAlone)
{
    const unsigned long long gigabyte = 1024ULL * 1024 * 1024;
    CHECK_EQUAL(std::string("3.2 GB / 15 GB"), ShownStorageUse(gigabyte * 32 / 10, 15 * gigabyte));
    CHECK_EQUAL(std::string("850 MB / 15 GB"),
                ShownStorageUse(850ULL * 1024 * 1024, 15 * gigabyte));
    CHECK_EQUAL(std::string("1 GB / 2048 GB"), ShownStorageUse(gigabyte, 2048 * gigabyte));
    // A storage without a limit tells only what it holds.
    CHECK_EQUAL(std::string("7.5 GB"), ShownStorageUse(gigabyte * 75 / 10, 0));
}

TEST(XmbSaveOfANameNoConsoleGivesIsOnlyListed)
{
    // Such a name, from a file put in the Drive by hand, could add a command to a script.
    const XmbSave odd = ListedSave("X;reboot", "Odd", "Odd", false);
    const XmbSave lower = ListedSave("bles00001-save", "Lower", "Lower", false);
    const XmbLists lists = MakeXmbLists(
        kXmbPlace, true, true, "", {XmbGame{"Odd", "BLES00001", {odd, lower}}}, kNewestByTitle);
    const std::string games = ListFile(lists, xmbGamesFile);
    CHECK(games.find("Odd") != std::string::npos);
    CHECK(games.find("Lower") != std::string::npos);
    CHECK(games.find("owns_save_0.bat") == std::string::npos);
    CHECK(games.find("owns_save_1.bat") == std::string::npos);
    for (const XmbScript &script : lists.scripts)
    {
        CHECK(script.key.compare(0, 10, "owns_save_") != 0);
    }
}

TEST(XmbListsWithoutWebmanOnlyListTheSaves)
{
    const XmbSave save = ListedSave("BLES00001-SAVE", "Game One", "Save", false);
    const XmbLists lists =
        MakeXmbLists(kXmbPlace, true, false, "", {XmbGame{"Game One", "BLES00001", {save}}},
                     XmbView{XmbSort::Title, XmbGroup::All});
    const std::string menu = ListFile(lists, xmbMenuFile);
    CHECK(menu.find("Install webMAN MOD") != std::string::npos);
    CHECK(menu.find("msg_sync_with_server") == std::string::npos);
    CHECK(menu.find("msg_online_storage") != std::string::npos);
    CHECK(ListFile(lists, XmbSavesVariant(kNewestByTitle)).find("Game One") != std::string::npos);
    CHECK(ListFile(lists, xmbSavesFile).find("Save") != std::string::npos);
    CHECK(ListFile(lists, xmbSavesFile).find("/play.ps3") == std::string::npos);
    CHECK(lists.scripts.empty());
}

TEST(XmbFolderGoesAfterTheSavedDataUtilityOnce)
{
    const std::string entry = XmbCategoryEntry(kXmbPlace);
    std::string category =
        "<XMBML>\r\n\t<View id=\"root\">\r\n\t\t<Items>\r\n"
        "\t\t\t<Query class=\"type:x-xmb/folder-pixmap\"\r\n"
        "\t\t\t\tkey=\"seg_sdps3\"\r\n\t\t\t\tsrc=\"#seg_sdps3\"\r\n\t\t\t\t/>\r\n"
        "\t\t\t<Query key=\"seg_welcome\"/>\r\n";
    CHECK(AddXmbCategoryEntry(category, entry));
    const std::size_t utility = category.find("key=\"seg_sdps3\"");
    const std::size_t ours = category.find("key=\"owns_root\"");
    CHECK(utility < ours && ours < category.find("key=\"seg_welcome\""));
    // The file's own line ends.
    CHECK(
        category.find("/>\r\n\t\t\t<Query class=\"type:x-xmb/folder-pixmap\" key=\"owns_root\"") !=
        std::string::npos);
    CHECK(!AddXmbCategoryEntry(category, entry));
    // Every view that lists the utility gets the folder, as the XMB shows one or another.
    std::string twoViews = "<View id=\"root\"><Items><Query key=\"seg_sdps3\"/>\n"
                           "<View id=\"root_for_BDU\"><Items><Query key=\"seg_sdps3\"/>\n";
    CHECK(AddXmbCategoryEntry(twoViews, entry));
    CHECK(twoViews.find("owns_root") != twoViews.rfind("owns_root"));
    // Without the utility it goes first; without the Game column's view, nowhere.
    std::string bare = "<View id=\"root\">\n\t\t<Items>\n\t\t\t<Query key=\"seg_gamedata\"/>\n";
    CHECK(AddXmbCategoryEntry(bare, entry));
    CHECK(bare.find("owns_root") < bare.find("seg_gamedata"));
    std::string none = "<XMBML></XMBML>";
    CHECK(!AddXmbCategoryEntry(none, entry));
}

TEST(XmbSaveActionsAreReadFromTheActionFile)
{
    XmbSaveAction what = XmbSaveAction::Delete;
    std::string name;
    // The row's own choice downloads, as X does in the Saved Data Utility.
    CHECK(ReadXmbSaveAction("save:BLES00001-SAVE", what, name));
    CHECK(what == XmbSaveAction::Download && name == "BLES00001-SAVE");
    CHECK(ReadXmbSaveAction("info:BLES00001-SAVE", what, name));
    CHECK(what == XmbSaveAction::Information);
    CHECK(ReadXmbSaveAction("delete:BLES00001-SAVE", what, name));
    CHECK(what == XmbSaveAction::Delete);
    CHECK(!ReadXmbSaveAction("delete:", what, name));
    CHECK(!ReadXmbSaveAction("sync", what, name));
}

TEST(XmbSaveInformationShowsWhatTheRowHasNoRoomFor)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSave) + "/PARAM.SFO",
                      MakeSfo(SaveTexts{"Game One", "Chapter 3", "Level 12\n"},
                              TestOwner(1, 'a', "0123456789abcdef")),
                      kWritten);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    Browsing there(setup, other);
    CHECK_EQUAL(std::string("Game One (BLES00001):\nChapter 3\n\nLevel 12"),
                XmbSaveInformation(there.browser, "BLES00001-SAVE"));
    // Nothing is downloaded, and nothing asked.
    CHECK(!other.Exists(std::string(kSavedata) + "/BLES00001-SAVE"));
    CHECK(setup.display.questions.empty());
    CHECK(XmbSaveInformation(there.browser, "BLES00009-GONE").empty());
}

TEST(XmbSaveDownloadAsksOnlyToReplaceADifferentCopy)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    {
        // Not on the console: downloaded without a question.
        Browsing there(setup, other);
        const Notice downloaded = DownloadXmbSave(there.browser, "BLES00001-SAVE");
        CHECK_EQUAL(std::string("BLES00001-SAVE is downloaded."), downloaded.text);
        CHECK(setup.display.questions.empty());
        // The notification has the system's words for it.
        CHECK_EQUAL(std::string("{system_plugin:msg_download_complete}"),
                    NotificationLine(downloaded));
        // On the console as the cloud has it: only said to be there.
        CHECK_EQUAL(std::string("BLES00001-SAVE is on this PS3 already."),
                    DownloadXmbSave(there.browser, "BLES00001-SAVE").text);
        CHECK(setup.display.questions.empty());
        CHECK(DownloadXmbSave(there.browser, "BLES00009-GONE").text.find("no longer") !=
              std::string::npos);
    }
    // Different on the console: the replace question.
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "played here", kChanged);
    Browsing here(setup, setup.console);
    setup.display.answers = {Answer::Yes};
    CHECK_EQUAL(std::string("BLES00001-SAVE is downloaded."),
                DownloadXmbSave(here.browser, "BLES00001-SAVE").text);
    CHECK(setup.display.questions.back().find("differs from the cloud's copy") !=
          std::string::npos);
}

TEST(XmbSaveDeleteKeepsASaveTheConsoleHasAndAsksNothing)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    {
        // On the console too: kept, as the next sync would upload it again.
        Browsing here(setup, setup.console);
        CHECK(DeleteXmbSave(here.browser, "BLES00001-SAVE").text.find("is on this PS3") !=
              std::string::npos);
    }
    // The user confirmed it on the XMB already.
    TestConsole other;
    other.MakeDirectories(kSavedata);
    Browsing there(setup, other);
    bool changed = false;
    CHECK_EQUAL(std::string("BLES00001-SAVE is in the Drive trash now."),
                DeleteXmbSave(there.browser, "BLES00001-SAVE", &changed).text);
    CHECK(changed);
    CHECK(setup.display.questions.empty());
    CHECK(DeleteXmbSave(there.browser, "BLES00001-SAVE").text.find("no longer") !=
          std::string::npos);
}

namespace
{
const XmbSort kAllSorts[] = {XmbSort::DateNewest, XmbSort::DateOldest, XmbSort::Title,
                             XmbSort::Subtitle};
const XmbGroup kAllGroups[] = {XmbGroup::ByTitle, XmbGroup::All};

/** The keys' numbers of a list file's save rows, in their order, such as "0,2,1". */
std::string SaveKeyOrder(const std::string &file)
{
    const std::string row = "<Item class=\"type:x-xmb/module-action\" key=\"owns_save_";
    std::string order;
    for (std::size_t at = file.find(row); at != std::string::npos; at = file.find(row, at + 1))
    {
        const std::size_t digits = at + row.size();
        order += (order.empty() ? "" : ",") + file.substr(digits, file.find('"', digits) - digits);
    }
    return order;
}

/** A row's attribute table in a list file; empty when it has none of the key. */
std::string RowTable(const std::string &file, const std::string &key)
{
    const std::size_t start = file.find("<Table key=\"" + key + "\">");
    return start == std::string::npos ? std::string()
                                      : file.substr(start, file.find("</Table>", start) - start);
}

/** Where a file is in the order the lists are written; past the end when they lack it. */
std::size_t FileIndex(const XmbLists &lists, const std::string &name)
{
    std::size_t index = 0;
    while (index < lists.files.size() && lists.files[index].name != name)
    {
        ++index;
    }
    return index;
}

/** A text with every from replaced by to. */
std::string Replaced(std::string text, const std::string &from, const std::string &to)
{
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size()))
    {
        text.replace(at, from.size(), to);
    }
    return text;
}

/**
 * Two games whose saves sort apart in every order. By title, newest first, the keys go: Alpha's
 * Zeta 0, Alpha's Beta 1, beta's Alpha 2.
 */
std::vector<XmbGame> SortableGames()
{
    return {
        XmbGame{"Alpha",
                "BLES00001",
                {ListedSave("BLES00001-A", "Alpha", "Zeta", false, "2026-03-01T10:00:00.000Z"),
                 ListedSave("BLES00001-B", "Alpha", "Beta", true, "2026-01-01T10:00:00.000Z")}},
        XmbGame{"beta",
                "BLUS00002",
                {ListedSave("BLUS00002-C", "beta", "Alpha", false, "2026-02-01T10:00:00.000Z")}}};
}
} // namespace

TEST(XmbViewIsReadFromTheLettersThePluginWrites)
{
    const XmbView oldest = ParseXmbView("ot");
    CHECK(oldest.sort == XmbSort::DateOldest && oldest.group == XmbGroup::ByTitle);
    const XmbView subtitle = ParseXmbView("sa\r\n");
    CHECK(subtitle.sort == XmbSort::Subtitle && subtitle.group == XmbGroup::All);
    CHECK(ParseXmbView("ta").sort == XmbSort::Title);
    // Anything else is the view before the user chose one.
    for (const char *text : {"", "d", "xa", "dx", "dta", "DT"})
    {
        const XmbView view = ParseXmbView(text);
        CHECK(view.sort == XmbSort::DateNewest && view.group == XmbGroup::ByTitle);
    }
}

TEST(XmbVariantsAreAsLongAsTheListsOwnFilesAndAllWritten)
{
    CHECK_EQUAL(std::string("owns_m_ta.xml"),
                XmbMenuVariant(XmbView{XmbSort::Title, XmbGroup::All}));
    CHECK_EQUAL(std::string("owns_sa_dt.xml"), XmbSavesVariant(kNewestByTitle));
    CHECK_EQUAL(std::string("owns_ga_st.xml"), XmbGamesVariant(XmbSort::Subtitle));
    const XmbLists lists = MakeXmbLists(kXmbPlace, true, true, "", SortableGames(), kNewestByTitle);
    std::vector<std::string> names;
    for (const XmbSort sort : kAllSorts)
    {
        CHECK_EQUAL(std::string(xmbGamesFile).size(), XmbGamesVariant(sort).size());
        names.push_back(XmbGamesVariant(sort));
        for (const XmbGroup group : kAllGroups)
        {
            const XmbView view{sort, group};
            CHECK_EQUAL(std::string(xmbMenuFile).size(), XmbMenuVariant(view).size());
            CHECK_EQUAL(std::string(xmbSavesFile).size(), XmbSavesVariant(view).size());
            names.push_back(XmbMenuVariant(view));
            names.push_back(XmbSavesVariant(view));
            // Each menu opens its own view's saves, which are written before it.
            CHECK(ListFile(lists, XmbMenuVariant(view))
                      .find(XmbSavesVariant(view) + "#owns_saves_view") != std::string::npos);
            CHECK(FileIndex(lists, XmbSavesVariant(view)) < FileIndex(lists, XmbMenuVariant(view)));
            CHECK(FileIndex(lists, XmbGamesVariant(sort)) <
                  FileIndex(lists, XmbSavesVariant(view)));
        }
    }
    names.insert(names.end(),
                 {xmbFolderFile, xmbMenuFile, xmbSavesFile, xmbGamesFile, xmbRowsFile});
    std::vector<std::string> written;
    for (const XmbFile &file : lists.files)
    {
        written.push_back(file.name);
    }
    std::sort(names.begin(), names.end());
    std::sort(written.begin(), written.end());
    CHECK(names == written);
    CHECK(FileIndex(lists, xmbGamesFile) < FileIndex(lists, xmbSavesFile));
    CHECK(FileIndex(lists, xmbSavesFile) < FileIndex(lists, xmbMenuFile));
    CHECK(FileIndex(lists, xmbMenuFile) < FileIndex(lists, xmbFolderFile));
}

TEST(XmbAllSavesAreListedInOneListInEverySortWithTheSameRows)
{
    const XmbLists lists = MakeXmbLists(kXmbPlace, true, true, "", SortableGames(), kNewestByTitle);
    const auto all = [&lists](XmbSort sort)
    { return ListFile(lists, XmbSavesVariant(XmbView{sort, XmbGroup::All})); };
    CHECK_EQUAL(std::string("0,2,1"), SaveKeyOrder(all(XmbSort::DateNewest)));
    CHECK_EQUAL(std::string("1,2,0"), SaveKeyOrder(all(XmbSort::DateOldest)));
    // By title whatever its case, then by subtitle; and the other way round.
    CHECK_EQUAL(std::string("1,0,2"), SaveKeyOrder(all(XmbSort::Title)));
    CHECK_EQUAL(std::string("2,1,0"), SaveKeyOrder(all(XmbSort::Subtitle)));
    CHECK(all(XmbSort::Title).find("owns_game_") == std::string::npos);
    // A save's row is the same in every view, so its key and its script stay.
    const std::string zeta = RowTable(all(XmbSort::DateNewest), "owns_save_0");
    CHECK(zeta.find("<String>Zeta</String>") != std::string::npos);
    CHECK(zeta.find("owns_save_0.bat") != std::string::npos);
    for (const XmbSort sort : kAllSorts)
    {
        CHECK_EQUAL(zeta, RowTable(all(sort), "owns_save_0"));
        CHECK_EQUAL(zeta, RowTable(ListFile(lists, XmbGamesVariant(sort)), "owns_save_0"));
    }
    std::size_t saveScripts = 0;
    for (const XmbScript &script : lists.scripts)
    {
        saveScripts += script.key.compare(0, 10, "owns_save_") == 0 ? 1 : 0;
    }
    CHECK_EQUAL(std::size_t(3), saveScripts);
}

TEST(XmbGamesAreFoldersByTitleWithTheirSavesInEverySort)
{
    const XmbLists lists = MakeXmbLists(kXmbPlace, true, true, "", SortableGames(), kNewestByTitle);
    const std::string byTitle =
        ListFile(lists, XmbSavesVariant(XmbView{XmbSort::Title, XmbGroup::ByTitle}));
    CHECK(SaveKeyOrder(byTitle).empty());
    CHECK(byTitle.find("owns_ga_tt.xml#owns_game_0_view") != std::string::npos);
    CHECK(byTitle.find("owns_ga_tt.xml#owns_game_1_view") != std::string::npos);
    CHECK(byTitle.find("<String>Alpha</String>") < byTitle.find("<String>beta</String>"));
    // Within a game the saves go by the sort; its saves share the title, so they go by subtitle.
    const auto alpha = [&lists](XmbSort sort)
    {
        const std::string file = ListFile(lists, XmbGamesVariant(sort));
        return SaveKeyOrder(file.substr(0, file.find("owns_game_1_view")));
    };
    CHECK_EQUAL(std::string("0,1"), alpha(XmbSort::DateNewest));
    CHECK_EQUAL(std::string("1,0"), alpha(XmbSort::DateOldest));
    CHECK_EQUAL(std::string("1,0"), alpha(XmbSort::Title));
    CHECK_EQUAL(std::string("1,0"), alpha(XmbSort::Subtitle));
    CHECK_EQUAL(std::string("1,0,2"),
                SaveKeyOrder(ListFile(lists, XmbGamesVariant(XmbSort::DateOldest))));
}

TEST(XmbListsOwnFilesHoldTheChosenViewAndOpenEachOther)
{
    const XmbView all{XmbSort::Subtitle, XmbGroup::All};
    const XmbLists lists = MakeXmbLists(kXmbPlace, true, true, "", SortableGames(), all);
    CHECK_EQUAL(Replaced(ListFile(lists, XmbMenuVariant(all)), XmbSavesVariant(all), xmbSavesFile),
                ListFile(lists, xmbMenuFile));
    CHECK_EQUAL(ListFile(lists, XmbSavesVariant(all)), ListFile(lists, xmbSavesFile));
    CHECK_EQUAL(ListFile(lists, XmbGamesVariant(XmbSort::Subtitle)), ListFile(lists, xmbGamesFile));
    const XmbView grouped{XmbSort::DateOldest, XmbGroup::ByTitle};
    const XmbLists other = MakeXmbLists(kXmbPlace, true, true, "", SortableGames(), grouped);
    CHECK_EQUAL(Replaced(ListFile(other, XmbSavesVariant(grouped)),
                         XmbGamesVariant(XmbSort::DateOldest), xmbGamesFile),
                ListFile(other, xmbSavesFile));
    CHECK_EQUAL(ListFile(other, XmbGamesVariant(XmbSort::DateOldest)),
                ListFile(other, xmbGamesFile));
    CHECK(ListFile(other, xmbFolderFile).find(std::string(xmbMenuFile) + "#") != std::string::npos);
}

TEST(XmbRowsIndexTellsTheTextsOfEachRow)
{
    XmbSave save = ListedSave("BLES00001-SAVE", "Tab\tGame", "Sub\\way", true);
    save.info = "Sub\\way\n1/1/2026 10:00   64 KB";
    save.updated = "1/1/2026 10:00";
    save.size = "64 KB";
    save.detail = "Line 1\nLine 2";
    const XmbLists lists = MakeXmbLists(
        kXmbPlace, true, true, "", {XmbGame{"Tab\tGame", "BLES00001", {save}}}, kNewestByTitle);
    // A backslash, a tab and a line break in a field are escaped; tabs separate the fields.
    CHECK_EQUAL(std::string("G\towns_game_0\tBLES00001\tTab\\tGame\tBLES00001 | 1 save\t1\n"
                            "S\towns_save_0\tBLES00001-SAVE\tTab\\tGame\t"
                            "Sub\\\\way\\n1/1/2026 10:00   64 KB\tSub\\\\way\t1/1/2026 10:00\t"
                            "64 KB\tLine 1\\nLine 2\tBLES00001\n"),
                ListFile(lists, xmbRowsFile));
}

TEST(XmbSaveCarriesItsDetailWithoutTheLineBreaksAfterIt)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSave) + "/PARAM.SFO",
                      MakeSfo(SaveTexts{"Game One", "Chapter 3", "Level 12\nBoss\n\n"},
                              TestOwner(1, 'a', "0123456789abcdef")),
                      kWritten);
    setup.Run();
    Browsing here(setup, setup.console);
    const std::vector<XmbGame> games = XmbGames(here.browser, setup.console, kSavedata, kXmbDates);
    CHECK(games.size() == 2 && games[1].saves.size() == 1 &&
          games[1].saves[0].subtitle == "Chapter 3" &&
          games[1].saves[0].detail == "Level 12\nBoss");
}

TEST(XmbGameActionsAreReadFromTheActionFile)
{
    XmbGameAction what = XmbGameAction::DeleteAll;
    std::string titleId;
    CHECK(ReadXmbGameAction("downloadall:BLES00001", what, titleId));
    CHECK(what == XmbGameAction::DownloadAll && titleId == "BLES00001");
    CHECK(ReadXmbGameAction("deleteall:BLES00001", what, titleId));
    CHECK(what == XmbGameAction::DeleteAll);
    CHECK(!ReadXmbGameAction("deleteall:", what, titleId));
    CHECK(!ReadXmbGameAction("delete:BLES00001-SAVE", what, titleId));
    XmbSaveAction saveWhat = XmbSaveAction::Download;
    std::string name;
    CHECK(!ReadXmbSaveAction("deleteall:BLES00001", saveWhat, name));
}

TEST(XmbGameDownloadLoadsEverySaveAndAsksOnlyToReplace)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN", "data-3", kWritten);
    setup.Run();
    TestConsole other;
    other.MakeDirectories(kSavedata);
    Browsing there(setup, other);
    bool changed = false;
    CHECK_EQUAL(std::string("Downloaded 2 of 2 saves of Game One (BLES00001)."),
                DownloadXmbGame(there.browser, "BLES00001", &changed).text);
    CHECK(changed);
    CHECK(setup.display.questions.empty());
    CHECK_EQUAL(std::string("data-3"),
                other.Content(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN"));
    CHECK(!other.Exists(std::string(kSavedata) + "/BLUS00002-AUTO"));
    // Saves there already are counted, and change nothing.
    CHECK_EQUAL(std::string("Downloaded 2 of 2 saves of Game One (BLES00001)."),
                DownloadXmbGame(there.browser, "BLES00001", &changed).text);
    CHECK(!changed);
    // A different copy is asked about, and stays on No.
    other.Put(std::string(kSave) + "/DATA.BIN", "played there", kChanged);
    setup.display.answers = {Answer::No};
    CHECK_EQUAL(std::string("Downloaded 1 of 2 saves of Game One (BLES00001)."),
                DownloadXmbGame(there.browser, "BLES00001", &changed).text);
    CHECK(!changed);
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::string("played there"), other.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK(DownloadXmbGame(there.browser, "BLES00009", &changed).text.find("no longer") !=
          std::string::npos);
    CHECK(changed);
    CHECK_EQUAL(std::string("Downloaded 1 of 1 save of BLUS00002."),
                DownloadXmbGame(there.browser, "BLUS00002").text);
}

TEST(XmbGameDeleteTrashesOnlyWhatTheConsoleHasNotAndAsksNothing)
{
    Setup setup;
    PutSaves(setup.console);
    setup.console.Put(std::string(kSavedata) + "/BLES00001-SAVE2/DATA.BIN", "data-3", kWritten);
    setup.Run();
    {
        Browsing here(setup, setup.console);
        bool changed = true;
        CHECK_EQUAL(
            std::string("0 saves of Game One (BLES00001) moved to the Drive trash; 2 on this PS3 "
                        "kept."),
            DeleteXmbGame(here.browser, "BLES00001", &changed).text);
        CHECK(!changed);
    }
    setup.DeleteSave("BLES00001-SAVE2");
    Browsing here(setup, setup.console);
    bool changed = false;
    CHECK_EQUAL(
        std::string("1 save of Game One (BLES00001) moved to the Drive trash; 1 on this PS3 "
                    "kept."),
        DeleteXmbGame(here.browser, "BLES00001", &changed).text);
    CHECK(changed);
    CHECK(setup.cloud.Tree(setup.rootId).find("BLES00001-SAVE2.tar") == std::string::npos);
    CHECK(setup.cloud.Tree(setup.rootId).find("BLES00001-SAVE.tar") != std::string::npos);
    TestConsole other;
    other.MakeDirectories(kSavedata);
    Browsing there(setup, other);
    CHECK_EQUAL(std::string("1 save of BLUS00002 moved to the Drive trash."),
                DeleteXmbGame(there.browser, "BLUS00002").text);
    CHECK(setup.display.questions.empty());
    CHECK(DeleteXmbGame(there.browser, "BLUS00002").text.find("no longer") != std::string::npos);
}

TEST(ArchiveMadeAnewIsNotTakenForAChangeOfTheSave)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    // Another account after a sign-out, or another console, has an archive of its own.
    setup.cloud.Delete(setup.ArchiveId("BLES00001-SAVE"));
    setup.cloud.Store(TestCloudItem{"BLES00001-SAVE.tar", setup.rootId, octetMime,
                                    MakeTar({{"DATA.BIN", "someone else's"}})});
    setup.Run();
    CHECK_EQUAL(std::string("data-1"), setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
}

TEST(SizesAndTimesReadAsAPersonReadsThem)
{
    CHECK_EQUAL(std::string("3787 KB"), ShownSize(3878400));
    CHECK_EQUAL(std::string("9999 KB"), ShownSize(9999 * 1024 + 1023));
    CHECK_EQUAL(std::string("36 MB"), ShownSize(38 * 1000 * 1000));
    // The console's own: day first, 12-hour clock, an hour east of UTC.
    const DateStyle console{DateOrder::DayMonthYear, true, 60};
    CHECK_EQUAL(std::string("14/11/2026 11:56 AM"), ShownTime("2026-11-14T10:56:21.672Z", console));
    CHECK_EQUAL(std::string("29/12/2023 6:19 PM"), ShownTime("2023-12-29T17:19:00.000Z", console));
    // Midnight and noon, and a date that local time moves to the next day, month and year.
    CHECK_EQUAL(std::string("1/1/2024 12:30 AM"), ShownTime("2023-12-31T23:30:00.000Z", console));
    CHECK_EQUAL(std::string("14/11/2026 12:05 PM"), ShownTime("2026-11-14T11:05:00.000Z", console));
    CHECK_EQUAL(
        std::string("2024/2/29 12:05"),
        ShownTime("2024-02-29T13:05:00.000Z", DateStyle{DateOrder::YearMonthDay, false, -60}));
    CHECK_EQUAL(
        std::string("12/31/2023 23:30"),
        ShownTime("2023-12-31T23:30:00.000Z", DateStyle{DateOrder::MonthDayYear, false, 0}));
    // A time not in Drive's form, or out of range, is shown as it is.
    CHECK_EQUAL(std::string("soon"), ShownTime("soon", console));
    CHECK_EQUAL(std::string("2026-13-45T99:99"), ShownTime("2026-13-45T99:99", console));
}

namespace
{
const SaveTarget kHere{TestOwner(2, 'b', ""), std::string(kIdpsSize, '\x22')};
const char kForeignSave[] = "BLES00005-SAVE";

// Whether StoreForeignSave() damages a signature of the save's PARAM.PFD.
const bool kDamaged = true;
const bool kSound = false;

/**
 * Puts a save of another console's user in the cloud, signed with a disc hash key of its game's
 * own.
 * @param damaged kDamaged when a signature of its PARAM.PFD is not to hold, else kSound
 */
void StoreForeignSave(Setup &setup, bool damaged)
{
    const std::string sfo = MakeSfo("Game Five", TestOwner(1, 'a', "0123456789abcdef"));
    std::string pfd = MakePfd(sfo, std::string(kIdpsSize, '\x11'), std::string(16, '\x77'), 3);
    if (damaged)
    {
        pfd[0x7B60] ^= 1;
    }
    setup.cloud.Store(
        TestCloudItem{std::string(kForeignSave) + ".tar", setup.rootId, octetMime,
                      MakeTar({{paramSfoName, sfo}, {paramPfdName, pfd}, {"DATA.BIN", "five"}})});
}
} // namespace

TEST(SaveOfAnotherConsoleIsDownloadedAsThisUsersOwn)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    StoreForeignSave(setup, kSound);
    setup.target = &kHere;
    setup.Run();
    const std::string folder = std::string(kSavedata) + "/" + kForeignSave;
    SaveOwner owner;
    CHECK(ReadSfoOwner(setup.console.Content(folder + "/PARAM.SFO"), owner) &&
          owner == kHere.owner);
    CHECK_EQUAL(std::string("five"), setup.console.Content(folder + "/DATA.BIN"));
    // Made this user's as it was downloaded, the save is not taken for a change of the user's.
    const int creates = setup.cloud.Creates();
    setup.Run();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(SaveThatCannotBeMadeThisUsersIsDownloadedAsItIsAndIsNamed)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    StoreForeignSave(setup, kDamaged);
    SyncState state(setup.console.Scratch() + "/state.json",
                    setup.console.Scratch() + "/state.backup");
    state.Load();
    Sync sync(setup.cloud, state, setup.console, setup.display, setup.clock, setup.rootId,
              kSavedata, kOwnSave, setup.console.Scratch(), &kHere);
    CHECK(sync.Run());
    CHECK(sync.NotResigned() == std::vector<std::string>{kForeignSave});
    SaveOwner owner;
    CHECK(ReadSfoOwner(
              setup.console.Content(std::string(kSavedata) + "/" + kForeignSave + "/PARAM.SFO"),
              owner) &&
          owner.userId == 1);
}

TEST(BrowserSaysWhetherALoadedSaveWasMadeThisUsers)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    StoreForeignSave(setup, kSound);
    setup.target = &kHere;
    {
        Browsing browsing(setup, setup.console);
        CHECK(browsing.browser.Load(kForeignSave) == LoadResult::Loaded);
    }
    SaveOwner owner;
    CHECK(ReadSfoOwner(
              setup.console.Content(std::string(kSavedata) + "/" + kForeignSave + "/PARAM.SFO"),
              owner) &&
          owner == kHere.owner);

    Setup other;
    other.console.MakeDirectories(kSavedata);
    StoreForeignSave(other, kDamaged);
    other.target = &kHere;
    Browsing browsing(other, other.console);
    CHECK(browsing.browser.Load(kForeignSave) == LoadResult::LoadedNotResigned);
    CHECK(ShownResult(kForeignSave, LoadResult::LoadedNotResigned).text.find("as it was") !=
          std::string::npos);
}

TEST(BrowserDeletesFromTheCloudOnlyASaveTheConsoleHasNot)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    Browsing browsing(setup, setup.console);
    CHECK(browsing.browser.Remove("BLES00001-SAVE") == RemoveResult::OnConsole);
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    setup.DeleteSave("BLES00001-SAVE");
    CHECK(browsing.browser.Remove("BLES00001-SAVE") == RemoveResult::Removed);
    CHECK(browsing.state.Find("BLES00001-SAVE") == nullptr);
    CHECK_EQUAL(std::string("BLUS00002-AUTO.tar\n"), setup.cloud.Tree(setup.rootId));
    CHECK_EQUAL(std::size_t(1), browsing.browser.Games().size());
    // Its base went with it, so the next sync has nothing to ask about it.
    setup.Run();
    CHECK(setup.display.questions.empty());
    CHECK(
        ShownResult("BLES00001-SAVE", RemoveResult::OnConsole).text.find("delete it there first") !=
        std::string::npos);
}

TEST(BrowserListsGamesByTrimmedNameAndSavesNewestFirst)
{
    Setup setup;
    setup.cloud.Store(TestCloudItem{"BLES00009-A.tar", setup.rootId, octetMime, MakeTar({})});
    setup.cloud.Store(TestCloudItem{"BLES00009-B.tar", setup.rootId, octetMime, MakeTar({})});
    setup.cloud.Store(TestCloudItem{"BLUS00008-SAVE.tar", setup.rootId, octetMime, MakeTar({})});
    // A byte order would put "  Zebra" before "apple"; a case-blind one by name puts it after.
    setup.cloud.SetDescription(setup.ArchiveId("BLES00009-A"), "  Zebra");
    setup.cloud.SetDescription(setup.ArchiveId("BLES00009-B"), "  Zebra");
    setup.cloud.SetDescription(setup.ArchiveId("BLUS00008-SAVE"), "apple");
    // The save that sorts last by name was stored last.
    setup.cloud.SetModified(setup.ArchiveId("BLES00009-A"), "2026-01-01T10:00:00.000Z");
    setup.cloud.SetModified(setup.ArchiveId("BLES00009-B"), "2026-02-01T10:00:00.000Z");
    setup.console.MakeDirectories(kSavedata);
    Browsing browsing(setup, setup.console);
    const std::vector<CloudGame> games = browsing.browser.Games();
    CHECK_EQUAL(std::size_t(2), games.size());
    if (games.size() == 2)
    {
        CHECK_EQUAL(std::string("apple (BLUS00008)"), games[0].label);
        CHECK_EQUAL(std::string("Zebra"), games[1].name);
        CHECK(games[1].saves == (std::vector<std::string>{"BLES00009-B", "BLES00009-A"}));
    }
}

namespace
{
const char kUsbSaves[] = "/dev_usb000/PS3/SAVEDATA";
const char kSecondUsbSaves[] = "/dev_usb001/PS3/SAVEDATA";

/** Imports the saves a console's USB drive holds, as the menu does. */
ImportResult Import(Setup &setup, TestConsole &on)
{
    SyncState state(on.Scratch() + "/state.json", on.Scratch() + "/state.backup");
    state.Load();
    SaveImport import(setup.cloud, state, on, setup.display, setup.clock, setup.rootId, kOwnSave,
                      on.Scratch());
    ImportResult result{0, 0, 0, 0};
    CHECK(import.Run({kUsbSaves, kSecondUsbSaves}, result));
    return result;
}

/** Puts a save, a PARAM.SFO and a data file, in a drive's save folder. */
void PutUsbSave(TestConsole &console, const char *folder, const std::string &name,
                const std::string &data)
{
    const std::string save = std::string(folder) + "/" + name;
    console.Put(save + "/PARAM.SFO", MakeSfo("Game"), kWritten);
    console.Put(save + "/DATA.BIN", data, kWritten);
}
} // namespace

TEST(UsbSavesAreUploadedKeptInTheCloudOnly)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    // A copy of a save the cloud has already, and one it has not.
    PutUsbSave(setup.console, kUsbSaves, "BLES00001-SAVE", "old copy");
    PutUsbSave(setup.console, kUsbSaves, "NPEB00003-USB", "usb");
    // What a computer leaves on a drive is not a save.
    setup.console.Put(std::string(kUsbSaves) + "/.DS_Store", "finder", kWritten);
    setup.console.MakeDirectories(std::string(kUsbSaves) + "/EMPTY");
    const ImportResult result = Import(setup, setup.console);
    CHECK_EQUAL(std::size_t(1), result.imported);
    CHECK_EQUAL(std::size_t(1), result.skipped);
    CHECK_EQUAL(std::size_t(0), result.failed);
    CHECK_EQUAL(std::string("data-1"), setup.Archive("BLES00001-SAVE")[0].content);
    CHECK(setup.cloud.CloudOnly(setup.ArchiveId("NPEB00003-USB")));
    CHECK_EQUAL(CloudTime(kWritten), setup.cloud.Modified(setup.ArchiveId("NPEB00003-USB")));
    // A second import finds it all there.
    CHECK_EQUAL(std::size_t(2), Import(setup, setup.console).skipped);
}

TEST(UsbImportLeavesTheSavesTheCloudHasUnread)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    PutUsbSave(setup.console, kUsbSaves, "BLES00001-SAVE", "old copy");
    PutUsbSave(setup.console, kUsbSaves, "NPEB00003-USB", "usb");
    const ImportResult result = Import(setup, setup.console);
    CHECK_EQUAL(std::size_t(1), result.skipped);
    CHECK_EQUAL(std::size_t(1), result.imported);
    // Reading a save is listing its folder: the one the cloud has is not read.
    CHECK(!setup.console.WasListed(std::string(kUsbSaves) + "/BLES00001-SAVE"));
    CHECK(setup.console.WasListed(std::string(kUsbSaves) + "/NPEB00003-USB"));
}

TEST(SaveKeptInTheCloudOnlyIsDownloadedOnlyWhenPicked)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    PutUsbSave(setup.console, kUsbSaves, "NPEB00003-USB", "usb");
    Import(setup, setup.console);
    TestConsole other;
    other.MakeDirectories(kSavedata);
    setup.Run(other, kSavedata);
    CHECK(!other.Exists(std::string(kSavedata) + "/NPEB00003-USB"));
    CHECK(setup.display.questions.empty());
    {
        Browsing browsing(setup, other);
        CHECK(browsing.browser.Load("NPEB00003-USB") == LoadResult::Loaded);
    }
    CHECK_EQUAL(std::string("usb"),
                other.Content(std::string(kSavedata) + "/NPEB00003-USB/DATA.BIN"));
    // Played on and uploaded, it is an ordinary save again.
    other.Put(std::string(kSavedata) + "/NPEB00003-USB/DATA.BIN", "played on", kChanged);
    setup.Run(other, kSavedata);
    CHECK(!setup.cloud.CloudOnly(setup.ArchiveId("NPEB00003-USB")));
}

TEST(SaveOnTwoUsbDrivesIsUploadedOnceAndAnUnreadableOneIsCounted)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    PutUsbSave(setup.console, kUsbSaves, "NPEB00004-BOTH", "first");
    PutUsbSave(setup.console, kSecondUsbSaves, "NPEB00004-BOTH", "second");
    // A file name too long for an archive, so the save cannot be uploaded.
    PutUsbSave(setup.console, kSecondUsbSaves, "NPEB00005-BAD", "bad");
    setup.console.Put(std::string(kSecondUsbSaves) + "/NPEB00005-BAD/" + std::string(120, 'x'),
                      "bad", kWritten);
    const ImportResult result = Import(setup, setup.console);
    CHECK_EQUAL(std::size_t(1), result.imported);
    CHECK_EQUAL(std::size_t(1), result.duplicates);
    CHECK_EQUAL(std::size_t(1), result.failed);
    CHECK_EQUAL(std::string("first"), setup.Archive("NPEB00004-BOTH")[0].content);
}

TEST(SaveKeptInTheCloudOnlyThatTheConsoleHasTooIsTakenAsTheSame)
{
    Setup setup;
    PutUsbSave(setup.console, kUsbSaves, "NPEB00003-USB", "usb");
    Import(setup, setup.console);
    // The console has the same save, without a base: the archive tells they are one.
    PutUsbSave(setup.console, kSavedata, "NPEB00003-USB", "usb");
    setup.Run();
    CHECK(setup.display.questions.empty());
    CHECK_EQUAL(0, setup.cloud.Updates());
}
