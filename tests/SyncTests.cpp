// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"
#include "SaveFiles.h"
#include "TestCloud.h"
#include "TestConsole.h"

#include "app/CloudSaveText.h"
#include "sync/ParamPfd.h"
#include "sync/ParamSfo.h"
#include "sync/SaveBrowser.h"
#include "sync/SaveImport.h"
#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"
#include "sync/Tar.h"

#include <ctime>
#include <string>
#include <vector>

namespace
{
/** A display nobody looks at, which never asks to stop and answers from a script. */
class ScriptedDisplay : public SyncDisplay
{
  public:
    void SetText(ProgressBar, const std::string &) override {}
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
};

// Saves written before the first sync.
const std::time_t kWritten = std::time(nullptr) - 3600;
// A later change on the console.
const std::time_t kChanged = kWritten + 60;

// The app's own save in the logged-in user's savedata.
const char kOwnSave[] = "OWNS00001-PROFILE";
const char kSavedata[] = "/dev_hdd0/home/00000001/savedata";
const char kSave[] = "/dev_hdd0/home/00000001/savedata/BLES00001-SAVE";

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
    std::string rootId;
    /** Whom saves brought down are made to belong to; null to leave them as they are. */
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
        return Sync(cloud, state, on, display, rootId, savedata, kOwnSave, on.Scratch(), target)
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
/** The cloud's saves as the menu of one console of the user sees them. */
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

TEST(UnchangedRerunSendsNothing)
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

TEST(TouchedButSameSaveSendsNothing)
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

TEST(SaveChangedOnAnotherConsoleComesDown)
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
    // What came down is the base now: the next run has nothing to do.
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

TEST(SaveChangedOnBothSidesComesDownOnYes)
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

TEST(SaveChangedOnBothSidesGoesUpOnNo)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.ChangeOnBothSides();
    setup.display.answers = {Answer::No};
    setup.Run();
    CHECK_EQUAL(std::string("played here"), setup.Archive("BLES00001-SAVE")[0].content);
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
}

TEST(SaveDeletedOnTheConsoleIsNotBroughtBackWithoutAnAnswer)
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
    CHECK(setup.display.Question(0).find("another PS3 has sent a newer copy") != std::string::npos);
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
    CHECK_EQUAL(std::string("Game One"),
                setup.cloud.Description(setup.ArchiveId("BLES00001-SAVE")));
    // The title comes from the cloud once the console has no PARAM.SFO left to read.
    setup.DeleteSave("BLES00001-SAVE");
    setup.Run();
    CHECK_EQUAL(std::size_t(1), setup.display.questions.size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("Game One (BLES00001):\n"));
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

TEST(SaveGoneFromTheCloudGoesUpAgain)
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
    // Without a base, an archive that turns up again is new to this console and comes down.
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

TEST(OnlySaveArchivesComeDownAndOfTwoTheFirst)
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

TEST(MenuListsTheGamesWithTheirSaves)
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

TEST(MenuTellsHowEachSaveIsOnTheConsole)
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

TEST(MenuLoadsAMissingSaveWithoutAsking)
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
    // What came down is the base, as after a sync.
    const SaveBase *base = browsing.state.Find("BLES00001-SAVE");
    CHECK(base != nullptr && base->fileId == setup.ArchiveId("BLES00001-SAVE") &&
          !base->downloading);
    CHECK(browsing.browser.Load("BLES00001-SAVE") == LoadResult::AlreadyThere);
}

TEST(MenuAsksBeforeItReplacesADifferentSave)
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

TEST(SaveOfAnotherConsoleComesDownAsThisUsersOwn)
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
    // Made this user's as it came down, the save is not taken for a change of the user's.
    const int creates = setup.cloud.Creates();
    setup.Run();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(SaveThatCannotBeMadeThisUsersComesDownAsItIsAndIsNamed)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    StoreForeignSave(setup, kDamaged);
    SyncState state(setup.console.Scratch() + "/state.json",
                    setup.console.Scratch() + "/state.backup");
    state.Load();
    Sync sync(setup.cloud, state, setup.console, setup.display, setup.rootId, kSavedata, kOwnSave,
              setup.console.Scratch(), &kHere);
    CHECK(sync.Run());
    CHECK(sync.NotResigned() == std::vector<std::string>{kForeignSave});
    SaveOwner owner;
    CHECK(ReadSfoOwner(
              setup.console.Content(std::string(kSavedata) + "/" + kForeignSave + "/PARAM.SFO"),
              owner) &&
          owner.userId == 1);
}

TEST(MenuSaysWhetherALoadedSaveWasMadeThisUsers)
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
    CHECK(ShownResult(kForeignSave, LoadResult::LoadedNotResigned).find("as it was") !=
          std::string::npos);
}

TEST(MenuDeletesFromTheCloudOnlyASaveTheConsoleHasNot)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    Browsing browsing(setup, setup.console);
    CHECK(browsing.browser.Remove("BLES00001-SAVE") == RemoveResult::OnConsole);
    CHECK(setup.display.questions.empty());
    setup.DeleteSave("BLES00001-SAVE");
    setup.display.answers = {Answer::No, Answer::Yes};
    CHECK(browsing.browser.Remove("BLES00001-SAVE") == RemoveResult::Kept);
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    CHECK(browsing.browser.Remove("BLES00001-SAVE") == RemoveResult::Removed);
    CHECK(browsing.state.Find("BLES00001-SAVE") == nullptr);
    CHECK_EQUAL(std::string("BLUS00002-AUTO.tar\n"), setup.cloud.Tree(setup.rootId));
    CHECK_EQUAL(std::size_t(1), browsing.browser.Games().size());
    CHECK_EQUAL(std::size_t(0), setup.display.Question(0).find("Game One (BLES00001):\n"));
    // Its base went with it, so the next sync has nothing to ask about it.
    setup.Run();
    CHECK_EQUAL(std::size_t(2), setup.display.questions.size());
    CHECK(ShownResult("BLES00001-SAVE", RemoveResult::OnConsole).find("delete it there first") !=
          std::string::npos);
}

TEST(MenuListsGamesByTrimmedNameAndSavesNewestFirst)
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
    SaveImport import(setup.cloud, state, on, setup.display, setup.rootId, kOwnSave, on.Scratch());
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

TEST(UsbSavesGoUpKeptInTheCloudOnly)
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
    // A second import finds it all there.
    CHECK_EQUAL(std::size_t(2), Import(setup, setup.console).skipped);
}

TEST(SaveKeptInTheCloudOnlyComesDownOnlyFromTheMenu)
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
    // Played on and sent up, it is an ordinary save again.
    other.Put(std::string(kSavedata) + "/NPEB00003-USB/DATA.BIN", "played on", kChanged);
    setup.Run(other, kSavedata);
    CHECK(!setup.cloud.CloudOnly(setup.ArchiveId("NPEB00003-USB")));
}

TEST(SaveOnTwoUsbDrivesGoesUpOnceAndAnUnreadableOneIsCounted)
{
    Setup setup;
    setup.console.MakeDirectories(kSavedata);
    PutUsbSave(setup.console, kUsbSaves, "NPEB00004-BOTH", "first");
    PutUsbSave(setup.console, kSecondUsbSaves, "NPEB00004-BOTH", "second");
    // A file name too long for an archive, so the save cannot be sent.
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
