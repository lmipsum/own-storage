// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"
#include "TestCloud.h"
#include "TestConsole.h"

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
/** A display nobody looks at, which never asks to stop. */
class SilentDisplay : public SyncDisplay
{
  public:
    void SetText(ProgressBar, const std::string &) override {}
    void Reset(ProgressBar) override {}
    void Advance(ProgressBar, unsigned int) override {}
    bool Cancelled() const override
    {
        return false;
    }
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
    console.Put(std::string(kSave) + "/PARAM.SFO", "sfo-1", kWritten);
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
    SilentDisplay display;
    std::string rootId;

    Setup()
    {
        rootId = cloud.CreateFolder(driveFolderName, "root")["id"].Str();
    }

    /** One run of the app on a console, with the state that console kept. */
    bool Run(TestConsole &on, const std::string &savedata)
    {
        SyncState state(on.Scratch() + "/state.json", on.Scratch() + "/state.backup");
        state.Load();
        return Sync(cloud, state, on, display, rootId, savedata, kOwnSave, on.Scratch()).Run();
    }

    /** One run of the app on the logged-in user's console. */
    bool Run()
    {
        return Run(console, kSavedata);
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

TEST(SaveChangedOnBothSidesIsLeftAsItIs)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    TestConsole other;
    other.MakeDirectories("/dev_hdd0/home/00000001/savedata");
    setup.Run(other, kSavedata);
    other.Put(std::string(kSave) + "/DATA.BIN", "played there", kChanged);
    setup.Run(other, kSavedata);
    setup.console.Put(std::string(kSave) + "/DATA.BIN", "played here", kChanged);
    setup.Run();
    CHECK_EQUAL(std::string("played here"),
                setup.console.Content(std::string(kSave) + "/DATA.BIN"));
    CHECK_EQUAL(std::string("played there"), setup.Archive("BLES00001-SAVE")[0].content);
}

TEST(SaveDeletedOnTheConsoleIsNotBroughtBack)
{
    Setup setup;
    PutSaves(setup.console);
    setup.Run();
    setup.console.Remove(std::string(kSave) + "/DATA.BIN");
    setup.console.Remove(std::string(kSave) + "/PARAM.SFO");
    setup.console.RemoveDirectory(kSave);
    setup.Run();
    CHECK(!setup.console.Exists(kSave));
    CHECK_EQUAL(std::size_t(2), setup.Archive("BLES00001-SAVE").size());
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
