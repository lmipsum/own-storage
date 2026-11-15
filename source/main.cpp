/*
# Playstation 3 Cloud Drive
# Copyright (C) 2013-2014 Mohammad Haseeb aka MHAQS
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
*/

#include <iostream>
#include <cstdio>
#include <string.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>

#include <lv2/syscalls.h>
#include <lv2/thread.h>
#include <sys/systime.h>
#include <sys/file.h>
#include <sys/thread.h>

#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <sysutil/save.h>

#include <net/net.h>
#include <net/netctl.h>
#include <netinet/in.h>
#include <atomic>
#include <optional>
#include <sstream>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "auth/ClientCredentials.h"
#include "drive/GoogleDrive.h"
#include "app/Files.h"
#include "app/Config.h"
#include "auth/GoogleDeviceAuth.h"
#include "net/HttpClient.h"
#include "app/CloudSaveText.h"
#include "app/Cobra.h"
#include "app/ConsoleStorage.h"
#include "app/ConsoleIdentity.h"
#include "app/ConsoleSettings.h"
#include "app/NetworkStart.h"
#include "app/Profile.h"
#include "app/LauncherSetup.h"
#include "app/Notice.h"
#include "app/XmbInstall.h"
#include "app/XmbPick.h"
#include "sync/Clock.h"
#include "sync/SaveBrowser.h"
#include "sync/SaveImport.h"
#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncState.h"
#include "json/Json.h"
#include "app/DebugLog.h"
#include "app/Paths.h"
#include "ui/Screen.h"
#include "ui/SyncDialog.h"
#include "ui/TransferBar.h"

const std::string appPath = APP_DIR;
// A Google client of the user's own, which takes the place of the published one.
const std::string userClientFile = APP_DIR "client.json";
// Uploads that run at once; Drive takes about a second to answer each, and these waits
// overlap. Eight went 1.5 times as fast as four, with no request refused.
const unsigned int parallelUploads = 8;
const unsigned int microsecondsPerSecond = 1000000;
// How often a wait checks whether the user has closed the sign-in message.
const unsigned int backPollMicroseconds = 100000;

HttpClient http;
GoogleDeviceAuth googleAuth(http);
GoogleDrive drive(http, googleAuth, parallelUploads, MakeTransferBar());

SDL_Surface *screen = NULL;
TTF_Font *Sans;

// The app thread and the main thread both use this flag.
std::atomic<int> exitapp(0);

SyncDialog syncDialog(exitapp);
const SystemClock systemClock;
NetworkStart networkStart(exitapp);

const char targetUnknownMessage[] = "This PS3's or the user's ids cannot be read, so saves of "
                                    "other users are downloaded as they are.";

// Where the XMB's lists go, beside webMAN MOD's own, and how their rows start the app: the launch
// request is the file plugin/launcher waits for.
const XmbPlace xmbPlace{"/dev_hdd0/xmlhost/game_plugin/", APP_DIR "xmb/", APP_DIR "xmb_action.txt",
                        "/dev_hdd0/tmp/owns_launch"};
// webMAN MOD's proxy, which runs what the XMB's rows ask for.
const char webmanProxy[] = "/dev_hdd0/tmp/wm_res/wm_proxy.sprx";
// The plugin the rows start the app through, which Cobra loads as the console starts.
const LauncherPlace launcherPlace{APP_DIR "plugins/owns_launcher.sprx",
                                  "/dev_hdd0/plugins/owns_launcher.sprx",
                                  "/dev_hdd0/boot_plugins.txt", APP_DIR "boot_plugins.bak"};
// Kept once the user has been told the console lacks Cobra, so it is told once.
const char cobraMissingTold[] = APP_DIR "cobra_missing.txt";
// The file the launcher plugin shows notifications from, a line each, as plugin/launcher reads it.
const char notificationRequest[] = "/dev_hdd0/tmp/owns_notify";
// Whether the launcher plugin runs, and so can show notifications: it is set up unchanged since
// the console started, and has taken every notification so far.
bool launcherRuns = false;
// How long the launcher plugin takes at most to take a notification: two of its polls.
const unsigned int notificationWaitMicroseconds = 700000;

const char driveFullMessage[] = "Your Google storage is full, so some saves were not backed up. "
                                "Free up space in Google Drive, then sync again.";
const char driveUnreachableMessage[] = "Cannot reach Google Drive. Check the network.";
const char driveRefusedMessage[] = "Google Drive refused a request. Try again later.";
const char signInExpiredMessage[] =
    "The Google sign-in is no longer valid. Sign out, then sign in again.";
const char localFileMessage[] =
    "Own Storage cannot write to the PS3 hard disk. Check the free space.";
const char unknownUserMessage[] = "Own Storage cannot tell which user is logged in.";

int appWidth = 1280;
int appHeight = 720;

typedef struct
{
    uint32_t total;
    uint32_t avail;
} _meminfo;
_meminfo meminfo;

void get_free_memory()
{
    lv2syscall1(SYSCALL_MEMORY_GET_USER_MEMORY_SIZE, (uint64_t)&meminfo);
}

void sysevent_callback(u64 status, u64 param, void *userdata)
{
    networkStart.OnSystemEvent(status);
    switch (status)
    {
    case SYSUTIL_EXIT_GAME:
        exitapp = 1;
        break;
    }
}

/** The likely cause of the last failed Drive request, as the user reads it. */
const char *driveFailureMessage()
{
    switch (drive.LastFailure())
    {
    case Failure::LocalFile:
        return localFileMessage;
    case Failure::Unreachable:
        return driveUnreachableMessage;
    default:
        return googleAuth.SignInRefused() ? signInExpiredMessage : driveRefusedMessage;
    }
}

/** Where a user's sync state is kept. */
std::string stateFile(const std::string &profile)
{
    return profile + "/state.json";
}

/** Where the backup of a user's sync state is kept. */
std::string stateBackup(const std::string &profile)
{
    return profile + "/state.backup";
}

/** Shows why the app cannot go on, and closes it once the user has read that. */
void stopWith(const std::string &message)
{
    syncDialog.ShowError(message);
    syncDialog.Close();
    exitapp = 1;
}

/**
 * Signs the user in with a code to enter on another device, and keeps the sign-in for the next
 * runs. The user may close the code's message to stop.
 * @param screen says what the app is doing
 * @param profile the user's directory of the app
 * @param config receives the sign-in
 * @return what came of it, as the user is told it
 */
Notice signIn(Screen &screen, const std::string &profile, Config &config)
{
    screen.ShowMessage("Asking Google for a sign-in code...");
    const std::string code = googleAuth.Start();
    if (code.empty())
    {
        return Problem("Cannot reach Google. Check the network.");
    }
    if (!syncDialog.ShowWaiting("On a phone or computer, open " + googleAuth.VerificationUrl() +
                                " and enter this code:\n\n" + code))
    {
        return Result("Sign-in cancelled.");
    }
    GoogleDeviceAuth::Status status = GoogleDeviceAuth::Pending;
    while (status == GoogleDeviceAuth::Pending)
    {
        // Google asks for seconds between polls; the user should not wait that long to go back.
        for (unsigned int waited = 0;
             waited < googleAuth.PollIntervalSeconds() * microsecondsPerSecond;
             waited += backPollMicroseconds)
        {
            if (exitapp != 0 || syncDialog.WaitingClosed())
            {
                syncDialog.Close();
                return Result("Sign-in cancelled.");
            }
            sysUsleep(backPollMicroseconds);
        }
        status = googleAuth.Poll();
    }
    syncDialog.Close();
    if (status != GoogleDeviceAuth::Approved)
    {
        return Problem("The sign-in did not go through. Try again.");
    }
    config.refreshToken = googleAuth.RefreshToken();
    SaveConfig(profile, config);
    return Result("Signed in.", "{system_plugin:msg_sigined_in}");
}

/**
 * Forgets the user's sign-in, so that the user may sign in again, with another account too.
 * @return what came of it, as the user is told it
 */
Notice signOut(const std::string &profile, Config &config)
{
    config = Config();
    SaveConfig(profile, config);
    googleAuth.SetRefreshToken(std::string());
    return Result("Signed out.", "{system_plugin:msg_signed_out}");
}

/**
 * Finds the Own Storage folder in the user's Drive.
 * @param create whether to create it when it is not there
 * @param folderId receives its id; empty when it is not there and was not to be created
 * @return false when Drive could not be asked
 */
bool findBackupFolder(bool create, std::string &folderId)
{
    Json folder;
    if (!drive.FindFolder(driveFolderName, "root", folder))
    {
        return false;
    }
    if (!folder.Has("id") && create)
    {
        folder = drive.CreateFolder(driveFolderName, "root");
        if (!folder.Has("id"))
        {
            return false;
        }
    }
    folderId = folder["id"].Str();
    return true;
}

/**
 * Brings the user's saves and their backup in line.
 * @return what came of it, as the user is told it
 */
Notice runSync(Screen &screen, ConsoleStorage &storage, const std::string &profile,
               const std::string &savedata, const SaveTarget *target)
{
    screen.ShowMessage("Syncing... Do not quit or switch off the PS3.");
    syncDialog.Open();
    std::string folderId;
    std::size_t notResigned = 0;
    bool done = findBackupFolder(true, folderId);
    if (done)
    {
        // Without a state of its own every save starts without a base; the sync works it out.
        SyncState state(stateFile(profile), stateBackup(profile));
        state.Load();
        Sync sync(drive, state, storage, syncDialog, systemClock, folderId, savedata,
                  profileMarkerSave, profile, target);
        done = sync.Run();
        notResigned = sync.NotResigned().size();
    }
    const bool full = done && drive.IsFull();
    if (full)
    {
        syncDialog.ShowError(driveFullMessage);
    }
    syncDialog.Close();
    if (!done)
    {
        return Problem(driveFailureMessage());
    }
    if (full)
    {
        return Result("The sync stopped: the Google storage is full.");
    }
    if (syncDialog.Cancelled())
    {
        return Result("The sync was stopped.");
    }
    if (notResigned == 1)
    {
        return Result("The sync is done; a save of another user was downloaded as it was, and "
                      "the game may refuse it.");
    }
    if (notResigned > 1)
    {
        std::ostringstream text;
        text << "The sync is done; " << notResigned
             << " saves of other users were downloaded as they were, and the games may refuse "
                "them.";
        return Result(text.str());
    }
    return Result("The sync is done.",
                  "{explore_plugin:msg_sync_with_server}: {explore_plugin:msg_completed_count}");
}

// The USB drives the console can have: /dev_usb000 to /dev_usb007.
const char lastUsbDrive = '7';

/**
 * Uploads the saves on USB drives into the backup, marked as kept in the cloud only.
 * @param storage the console's disks
 * @param state the user's bases
 * @param profile the user's directory of the app, which holds the archives while they move
 * @param folderId the cloud id of the Own Storage folder
 * @return what came of it, as the user is told it
 */
Notice importUsbSaves(ConsoleStorage &storage, SyncState &state, const std::string &profile,
                      const std::string &folderId)
{
    std::vector<std::string> folders;
    for (char number = '0'; number <= lastUsbDrive; ++number)
    {
        const std::string folder = std::string("/dev_usb00") + number + usbSavedataDirectory;
        if (!storage.List(folder).empty())
        {
            folders.push_back(folder);
        }
    }
    if (folders.empty())
    {
        return Result("No USB drive with saves in PS3/SAVEDATA was found.",
                      "{explore_plugin:msg_usb}: {explore_plugin:msg_error_no_savedata}");
    }
    syncDialog.Open();
    SaveImport import(drive, state, storage, syncDialog, systemClock, folderId, profileMarkerSave,
                      profile);
    ImportResult result;
    if (!import.Run(folders, result))
    {
        return Problem(driveFailureMessage());
    }
    if (drive.IsFull())
    {
        syncDialog.ShowError(driveFullMessage);
    }
    std::ostringstream text;
    text << result.imported << (result.imported == 1 ? " save" : " saves") << " imported, "
         << result.skipped << " already in the cloud left as they are";
    if (result.duplicates > 0)
    {
        text << ", " << result.duplicates << " on two drives uploaded once";
    }
    if (result.failed > 0)
    {
        text << ", " << result.failed << " not uploaded";
    }
    const bool whole = !drive.IsFull() && !syncDialog.Cancelled() && result.failed == 0;
    text << (drive.IsFull()           ? ": the Google storage is full."
             : syncDialog.Cancelled() ? ": the import was stopped."
                                      : ".");
    // The system's words leave the counts out, so they tell only an import that went through.
    return Result(text.str(), whole ? "{explore_plugin:msg_imported}" : std::string());
}

/**
 * Lists the saves in the cloud for the XMB anew. When they cannot be listed the XMB keeps the
 * lists it has, which are better than none.
 * @param signedIn whether the user is signed in, without which only the sign-in row is listed
 */
void refreshXmb(ConsoleStorage &storage, const std::string &profile, const std::string &savedata,
                const SaveTarget *target, bool signedIn)
{
    std::vector<XmbSave> saves;
    if (signedIn)
    {
        std::string folderId;
        if (!findBackupFolder(false, folderId))
        {
            return;
        }
        SyncState state(stateFile(profile), stateBackup(profile));
        state.Load();
        SaveBrowser browser(drive, state, storage, syncDialog, folderId, savedata,
                            profileMarkerSave, profile, target);
        // A user who has never synced has no backup folder, and so no saves to list.
        if (!folderId.empty() && !browser.Refresh())
        {
            return;
        }
        saves = XmbSaves(browser, storage, savedata, ReadDateStyle());
    }
    // A row needs webMAN to run its script, and Cobra to load the plugin that starts the app.
    FileStatus proxy;
    const bool rowsRun = storage.Status(webmanProxy, proxy) && CobraRuns();
    // Not known, the storage's use is left out rather than kept from a run long gone.
    std::string storageUse;
    unsigned long long used = 0;
    unsigned long long total = 0;
    if (signedIn && drive.StorageUse(used, total))
    {
        storageUse = ShownStorageUse(used, total);
    }
    WriteXmbLists(storage, xmbPlace, MakeXmbLists(xmbPlace, signedIn, rowsRun, storageUse, saves));
}

/**
 * Does what a row of the XMB asked for.
 * @param action the action the row left
 * @return what came of it, as the user is told it; empty for an action this app does not know
 */
Notice runXmbAction(Screen &screen, ConsoleStorage &storage, const std::string &profile,
                    const std::string &savedata, const SaveTarget *target, Config &config,
                    const std::string &action)
{
    if (action == xmbSignIn)
    {
        return signIn(screen, profile, config);
    }
    if (config.refreshToken.empty())
    {
        return Result("Sign in to Google Drive first.",
                      "{explore_plugin:msg_error_cannot_operate_signin_pls}");
    }
    if (action == xmbSignOut)
    {
        return signOut(profile, config);
    }
    if (action == xmbSync)
    {
        return runSync(screen, storage, profile, savedata, target);
    }
    std::string folderId;
    const bool isSave = action.compare(0, sizeof xmbSave - 1, xmbSave) == 0;
    if (action != xmbUsbImport && !isSave)
    {
        return Result(std::string());
    }
    // An import creates the folder when it is missing, so that saves can go into it.
    if (!findBackupFolder(!isSave, folderId))
    {
        return Problem(driveFailureMessage());
    }
    SyncState state(stateFile(profile), stateBackup(profile));
    state.Load();
    if (!isSave)
    {
        return importUsbSaves(storage, state, profile, folderId);
    }
    SaveBrowser browser(drive, state, storage, syncDialog, folderId, savedata, profileMarkerSave,
                        profile, target);
    if (folderId.empty())
    {
        return Result("There are no saves in the cloud.", "{explore_plugin:msg_error_no_savedata}");
    }
    if (!browser.Refresh())
    {
        return Problem(driveFailureMessage());
    }
    return PickXmbSave(browser, syncDialog, action.substr(sizeof xmbSave - 1));
}

/**
 * Tells the user what came of something: a notification for a result, while the launcher plugin
 * runs to show it; a dialog for a problem, which should not be missed, or without the plugin.
 * @param storage the console's hard disk
 * @param notice what to tell
 */
void tell(ConsoleStorage &storage, const Notice &notice)
{
    if (notice.text.empty())
    {
        return;
    }
    if (!notice.problem && launcherRuns &&
        storage.Append(notificationRequest, NotificationLine(notice) + "\n"))
    {
        // The plugin takes it at once, unless the user put off the restart that loads it; then
        // the rest of the run tells in dialogs.
        FileStatus left;
        for (unsigned int waited = 0; waited < notificationWaitMicroseconds;
             waited += backPollMicroseconds)
        {
            sysUsleep(backPollMicroseconds);
            if (!storage.Status(notificationRequest, left))
            {
                return;
            }
        }
        storage.Remove(notificationRequest);
        launcherRuns = false;
    }
    syncDialog.ShowInfo(notice.text);
}

/**
 * Puts the plugin the XMB's rows start the app through in place, and offers to restart the console
 * when it changed, as only a start loads it. Without Cobra nothing loads it, which the user is told
 * once.
 * @param storage the console's hard disk
 */
void setUpLauncher(ConsoleStorage &storage)
{
    if (!CobraRuns())
    {
        FileStatus told;
        if (!storage.Status(cobraMissingTold, told))
        {
            syncDialog.ShowInfo("The XMB rows of Own Storage need a CFW with Cobra.");
            storage.Write(cobraMissingTold, "");
        }
        return;
    }
    const LauncherSetup setup = SetUpLauncher(storage, launcherPlace);
    // Unchanged, the plugin has run since the console started; changed, only from the next start.
    launcherRuns = setup == LauncherSetup::Unchanged;
    if (setup == LauncherSetup::Changed &&
        syncDialog.Ask("Own Storage has set up its XMB helper. Restart the PS3 now?") ==
            Answer::Yes)
    {
        RestartConsole();
    }
}

/**
 * The app on its own thread: sees that the rows' launcher plugin is in place, finds the logged-in
 * user, does what the XMB's row asked for, lists the saves in the cloud for the XMB anew and goes
 * back to the XMB. Started from its own icon, it only lists them, or signs the user in first.
 * @param screen says what the app is doing behind the system's dialogs
 */
void runApp(Screen &screen)
{
    // It needs no network or user, so a run that stops early does it too.
    ConsoleStorage consoleStorage;
    setUpLauncher(consoleStorage);
    if (exitapp != 0)
    {
        return;
    }
    if (!networkStart.Connect())
    {
        stopWith("Own Storage needs a network connection.");
        return;
    }
    ClientCredentials client = LoadUserCredentials(userClientFile, "google");
    if (!client.IsComplete())
    {
        client = FetchPublishedCredentials(http, "google");
    }
    if (!client.IsComplete())
    {
        stopWith("Cannot reach the Own Storage configuration. Check the network.");
        return;
    }
    googleAuth.SetClient(client);

    const std::optional<std::string> user = FindLoggedInUser(consoleStorage);
    if (!user)
    {
        stopWith(unknownUserMessage);
        return;
    }
    const std::string profile = MakeProfileDirectory(consoleStorage, *user);
    const std::string savedata = SavedataDirectory(*user);
    // Saves of another user or console are made this user's as they are downloaded.
    const std::optional<SaveTarget> target = ReadSaveTarget(*user);
    const SaveTarget *saveTarget = target ? &*target : nullptr;
    Config config;
    LoadConfig(profile, config);
    // The first request finds the access token missing and gets one with this.
    googleAuth.SetRefreshToken(config.refreshToken);

    if (!InstallXmbFolder(consoleStorage, xmbPlace))
    {
        syncDialog.ShowError("Own Storage cannot put its folder in the XMB.");
    }
    std::string action = TakeXmbAction(consoleStorage, xmbPlace);
    if (action.empty() && config.refreshToken.empty())
    {
        // Without a sign-in the XMB has nothing to list.
        action = xmbSignIn;
    }
    if (!action.empty())
    {
        tell(consoleStorage,
             runXmbAction(screen, consoleStorage, profile, savedata, saveTarget, config, action));
    }
    // A quit from the XMB leaves the lists as they are: the system may end the app any time.
    if (exitapp != 0)
    {
        return;
    }
    screen.ShowMessage("Updating the list...");
    refreshXmb(consoleStorage, profile, savedata, saveTarget, !config.refreshToken.empty());
    if (saveTarget == nullptr)
    {
        tell(consoleStorage, Result(targetUnknownMessage));
    }
    syncDialog.Close();
    exitapp = 1;
}

// SDL gives its threads a 16 KB stack, too small for the app's work.
const u64 appThreadStackSize = 256 * 1024;
// The priority SDL gave the thread, so the app shares the console as it did.
const s32 appThreadPriority = 1500;

/**
 * Runs the app on a thread of its own; see runApp.
 * @param screen the Screen
 */
void runAppThread(void *screen)
{
    runApp(*static_cast<Screen *>(screen));
    sysThreadExit(0);
}

int main(int argc, char **argv)
{
    char FontPath[] = APP_DIR "font.ttf";

    netInitialize();
    debugInit();
    networkStart.Start();

    SDL_Init(SDL_INIT_EVERYTHING);
    TTF_Init();

    videoState state;
    videoGetState(0, 0, &state);
    videoResolution res;
    videoGetResolution(state.displayMode.resolution, &res);

    debugPrintf("Resolution %u,%u\n", res.width, res.height);

    appWidth = res.width;
    appHeight = res.height;

    screen =
        SDL_SetVideoMode(appWidth, appHeight, 32, SDL_SWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);

    Sans = TTF_OpenFont(FontPath, 20.0);

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysevent_callback, NULL);
    {
        // Gone before SDL shuts down, which the screen's surfaces need.
        Screen appScreen(screen, Sans);
        sys_ppu_thread_t appThread;
        const s32 created =
            sysThreadCreate(&appThread, runAppThread, &appScreen, appThreadPriority,
                            appThreadStackSize, THREAD_JOINABLE, const_cast<char *>("app"));
        if (created != 0)
        {
            debugPrintf("The app thread cannot start: %08x\n", static_cast<unsigned int>(created));
            exitapp = 1;
        }

        while (exitapp == 0)
        {
            sysUtilCheckCallback();
            appScreen.Draw();
            SDL_Flip(screen);
        }

        debugPrintf("The application exited...%d\n", exitapp.load());

        // The app's thread uses the screen, which goes when this block ends, and may
        // still open the sync dialog.
        if (created == 0)
        {
            u64 status;
            sysThreadJoin(appThread, &status);
        }
        syncDialog.Close();
    }
    SDL_FreeSurface(screen);
    TTF_CloseFont(Sans);

    TTF_Quit();
    SDL_Quit();

    networkStart.Stop();
    debugClose();
    netDeinitialize();

    return 0;
}
