// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"
#include "TestCloud.h"
#include "TestConsole.h"

#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncDisplay.h"
#include "sync/SyncState.h"

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

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

// Saves written before the first sync; the cloud's modifiedDate is later than this.
const std::time_t kWritten = std::time(nullptr) - 3600;

// The logged-in user, and the app's own save in that user's savedata.
const char kUser[] = "00000001";
const char kOwnSave[] = "OWNS00001-PROFILE";
const char kSavedata[] = "/dev_hdd0/home/00000001/savedata";

const char kSave[] = "/dev_hdd0/home/00000001/savedata/BLES00001-SAVE";

/**
 * Fills a console with the logged-in user's saves and the app's own save, another user's save
 * of the same name, and a memory card.
 */
void WriteSaves(TestConsole &console)
{
    console.Write(std::string(kSave) + "/PARAM.SFO", "sfo-1", kWritten);
    console.Write(std::string(kSave) + "/DATA.BIN", "data-1", kWritten);
    console.Write("/dev_hdd0/home/00000001/savedata/BLUS00002-AUTO/DATA.BIN", "data-2", kWritten);
    console.Write("/dev_hdd0/home/00000001/savedata/OWNS00001-PROFILE/MARKER.DAT", "1", kWritten);
    console.Write("/dev_hdd0/home/00000002/savedata/BLES00001-SAVE/DATA.BIN", "other user",
                  kWritten);
    console.Write("/dev_hdd0/savedata/vmc/CARD1.VM2", "card", kWritten);
}

/**
 * What the first sync of WriteSaves() leaves in the Own Storage folder: the logged-in user's
 * saves alone.
 */
const char kFirstSyncTree[] = "BLES00001-SAVE/\n"
                              "  DATA.BIN\n"
                              "  PARAM.SFO\n"
                              "BLUS00002-AUTO/\n"
                              "  DATA.BIN\n"
                              "remote.json\n";

/** The logged-in user's saves, as the app scans them. */
Json::Array Scan(const TestConsole &console)
{
    return SaveScanner(console, SavedataDirectory(kUser), kOwnSave).Scan();
}

/** One console and its cloud, with the Own Storage folder made, as a run sees them. */
struct Setup
{
    TestConsole console;
    TestCloud cloud;
    SilentDisplay display;
    std::string rootId;

    Setup() : cloud([this](const std::string &path) { return console.Map(path); })
    {
        rootId = cloud.CreateFolder(driveFolderName, "root")["id"].Str();
    }

    /** A fresh remote.json state, as a new run of the app reads it. */
    std::unique_ptr<SyncState> NewState() const
    {
        const std::string scratch = console.Scratch();
        return std::unique_ptr<SyncState>(new SyncState(
            scratch + "/remote.json", scratch + "/remote.backup", scratch + "/remote.journal"));
    }

    /** One later run: change detection, upload and download, as the app does it. */
    void RunAgain()
    {
        std::unique_ptr<SyncState> state = NewState();
        CHECK(state->Load());
        Sync sync(cloud, *state, console, display, rootId, kSavedata);
        sync.DetectChanges(Scan(console));
        sync.Upload();
        sync.Download();
    }

    /** The first run: everything the scan found goes up. */
    void RunFirst()
    {
        std::unique_ptr<SyncState> state = NewState();
        state->Reset(Scan(console));
        Sync(cloud, *state, console, display, rootId, kSavedata).Upload();
    }
};

/** Whether every entry of the state on disk is synced. */
bool AllSynced(const Setup &setup)
{
    std::unique_ptr<SyncState> state = setup.NewState();
    state->Load();
    for (std::size_t i = 0; i < state->Size(); i++)
    {
        if (state->Entry(i)["status"].Str() != syncedStatus)
        {
            return false;
        }
    }
    return state->Size() > 0;
}
} // namespace

TEST(FirstSyncUploadsTheLoggedInUsersSavesAlone)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.RunFirst();
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    CHECK(AllSynced(setup));
}

TEST(UnchangedRerunSendsNothing)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.RunFirst();
    const int creates = setup.cloud.Creates();
    setup.RunAgain();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK_EQUAL(0, setup.cloud.Updates());
}

TEST(ChangedFileIsUpdatedInPlace)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.RunFirst();
    const int creates = setup.cloud.Creates();
    setup.console.Write(std::string(kSave) + "/DATA.BIN", "data-1-changed",
                        std::time(nullptr) + 3600);
    setup.RunAgain();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    // The file and remote.json.
    CHECK_EQUAL(2, setup.cloud.Updates());
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
}

TEST(CreateWithLostAnswerIsTakenOverNextRun)
{
    Setup setup;
    WriteSaves(setup.console);
    // The second create after the Own Storage folder: the first save folder goes through, the
    // second is made but its answer is lost.
    setup.cloud.LoseAnswerOfCreate(1);
    setup.RunFirst();
    CHECK(!AllSynced(setup));
    setup.RunAgain();
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    CHECK(AllSynced(setup));
}

TEST(FailedSearchLeavesTheEntryForTheNextRun)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.cloud.LoseAnswerOfCreate(1);
    setup.RunFirst();
    const int creates = setup.cloud.Creates();
    setup.cloud.FailSearches(true);
    setup.RunAgain();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    setup.cloud.FailSearches(false);
    setup.RunAgain();
    CHECK_EQUAL(std::string(kFirstSyncTree), setup.cloud.Tree(setup.rootId));
    CHECK(AllSynced(setup));
}

TEST(NewConsoleGetsTheBackupBackUnderAnotherUserNumber)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.RunFirst();

    TestConsole newConsole;
    // The same person is user 2 there; the console made that user's savedata directory.
    newConsole.Write("/dev_hdd0/home/00000002/localusername", "two", kWritten);
    newConsole.MakeDirectory("/dev_hdd0/home/00000002/savedata");
    TestCloud &cloud = setup.cloud;
    const std::string scratch = newConsole.Scratch();
    SyncState state(scratch + "/remote.json", scratch + "/remote.backup",
                    scratch + "/remote.journal");
    // The cloud writes downloads where the new console keeps its files.
    TestCloud restoring([&newConsole](const std::string &path) { return newConsole.Map(path); });
    for (const auto &item : cloud.Items())
    {
        CHECK_EQUAL(item.first, restoring.Store(item.second));
    }
    Sync sync(restoring, state, newConsole, setup.display, setup.rootId,
              "/dev_hdd0/home/00000002/savedata");
    CHECK(sync.FetchState() == RemoteStateFetch::Done);
    sync.Download();
    CHECK_EQUAL(std::string("data-1"),
                newConsole.Read("/dev_hdd0/home/00000002/savedata/BLES00001-SAVE/DATA.BIN"));
    CHECK_EQUAL(std::string("data-2"),
                newConsole.Read("/dev_hdd0/home/00000002/savedata/BLUS00002-AUTO/DATA.BIN"));
}

TEST(LostLocalStateComesBackFromTheCloudWithoutDuplicates)
{
    Setup setup;
    WriteSaves(setup.console);
    setup.RunFirst();
    const int creates = setup.cloud.Creates();
    const std::string scratch = setup.console.Scratch();
    std::remove((scratch + "/remote.json").c_str());
    std::remove((scratch + "/remote.backup").c_str());

    // What the app does when it finds no state of its own.
    std::unique_ptr<SyncState> state = setup.NewState();
    CHECK(!state->Load());
    CHECK_EQUAL(std::size_t(0), state->Size());
    Sync sync(setup.cloud, *state, setup.console, setup.display, setup.rootId, kSavedata);
    CHECK(sync.FetchState() == RemoteStateFetch::Done);
    sync.DetectChanges(Scan(setup.console));
    sync.Upload();
    CHECK_EQUAL(creates, setup.cloud.Creates());
    CHECK(AllSynced(setup));
}

TEST(DamagedStateInTheCloudIsNotTaken)
{
    Setup setup;
    setup.cloud.Store(TestCloudItem{"remote.json", setup.rootId, jsonMime, "not a sync state"});
    std::unique_ptr<SyncState> state = setup.NewState();
    Sync sync(setup.cloud, *state, setup.console, setup.display, setup.rootId, kSavedata);
    CHECK(sync.FetchState() == RemoteStateFetch::Damaged);
    CHECK(!setup.console.Exists(state->FilePath()));
}

TEST(StateThatLeavesTheSavedataIsNotTaken)
{
    Setup setup;
    setup.cloud.Store(TestCloudItem{"remote.json", setup.rootId, jsonMime,
                                    "{\"data\":[{\"path\":\"BLES00001-SAVE/../../00000002\"}]}"});
    std::unique_ptr<SyncState> state = setup.NewState();
    Sync sync(setup.cloud, *state, setup.console, setup.display, setup.rootId, kSavedata);
    CHECK(sync.FetchState() == RemoteStateFetch::Damaged);
    CHECK(!setup.console.Exists(state->FilePath()));
}

TEST(JournalKeepsWhatAnInterruptedRunWasTold)
{
    Setup setup;
    WriteSaves(setup.console);
    std::unique_ptr<SyncState> state = setup.NewState();
    state->Reset(Scan(setup.console));
    Json folder;
    folder.Add("id", Json("folder-id"));
    state->RecordFolder(0, setup.rootId, folder);
    // No full save after this one change: a new run finds it in the journal.
    std::unique_ptr<SyncState> next = setup.NewState();
    CHECK(next->Load());
    CHECK_EQUAL(std::string("folder-id"), next->Entry(0)["id"].Str());
    CHECK_EQUAL(syncedStatus, next->Entry(0)["status"].Str());
}
