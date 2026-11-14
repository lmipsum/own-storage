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
#include "app/ConsoleStorage.h"
#include "app/ConsoleIdentity.h"
#include "app/ConsoleSettings.h"
#include "app/NetworkStart.h"
#include "app/Profile.h"
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

// Where the XMB's lists go, beside webMAN MOD's own, and how their rows start the app.
const XmbPlace xmbPlace{"/dev_hdd0/xmlhost/game_plugin/", APP_DIR "xmb/", APP_DIR "xmb_action.txt",
                        APP_ID};
// webMAN MOD's proxy, which runs what the XMB's rows ask for.
const char webmanProxy[] = "/dev_hdd0/tmp/wm_res/wm_proxy.sprx";

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
 * @return what came of it, as the user reads it
 */
std::string signIn(Screen &screen, const std::string &profile, Config &config)
{
    screen.ShowMessage("Asking Google for a sign-in code...");
    const std::string code = googleAuth.Start();
    if (code.empty())
    {
        return "Cannot reach Google. Check the network.";
    }
    if (!syncDialog.ShowWaiting("On a phone or computer, open " + googleAuth.VerificationUrl() +
                                " and enter this code:\n\n" + code))
    {
        return "Sign-in cancelled.";
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
                return "Sign-in cancelled.";
            }
            sysUsleep(backPollMicroseconds);
        }
        status = googleAuth.Poll();
    }
    syncDialog.Close();
    if (status != GoogleDeviceAuth::Approved)
    {
        return "The sign-in did not go through. Try again.";
    }
    config.refreshToken = googleAuth.RefreshToken();
    SaveConfig(profile, config);
    return "Signed in.";
}

/**
 * Forgets the user's sign-in, so that the user may sign in again, with another account too.
 * @return what came of it, as the user reads it
 */
std::string signOut(const std::string &profile, Config &config)
{
    config = Config();
    SaveConfig(profile, config);
    googleAuth.SetRefreshToken(std::string());
    return "Signed out.";
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
 * @return what came of it, as the user reads it
 */
std::string runSync(Screen &screen, ConsoleStorage &storage, const std::string &profile,
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
        return driveFailureMessage();
    }
    if (full)
    {
        return "The sync stopped: the Google storage is full.";
    }
    if (syncDialog.Cancelled())
    {
        return "The sync was stopped.";
    }
    if (notResigned == 1)
    {
        return "The sync is done; a save of another user was downloaded as it was, and the "
               "game may refuse it.";
    }
    if (notResigned > 1)
    {
        std::ostringstream text;
        text << "The sync is done; " << notResigned
             << " saves of other users were downloaded as they were, and the games may refuse "
                "them.";
        return text.str();
    }
    return "The sync is done.";
}

// The USB drives the console can have: /dev_usb000 to /dev_usb007.
const char lastUsbDrive = '7';

/**
 * Uploads the saves on USB drives into the backup, marked as kept in the cloud only.
 * @param storage the console's disks
 * @param state the user's bases
 * @param profile the user's directory of the app, which holds the archives while they move
 * @param folderId the cloud id of the Own Storage folder
 * @return what came of it, as the user reads it
 */
std::string importUsbSaves(ConsoleStorage &storage, SyncState &state, const std::string &profile,
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
        return "No USB drive with saves in PS3/SAVEDATA was found.";
    }
    syncDialog.Open();
    SaveImport import(drive, state, storage, syncDialog, systemClock, folderId, profileMarkerSave,
                      profile);
    ImportResult result;
    if (!import.Run(folders, result))
    {
        return driveFailureMessage();
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
    text << (drive.IsFull()           ? ": the Google storage is full."
             : syncDialog.Cancelled() ? ": the import was stopped."
                                      : ".");
    return text.str();
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
    FileStatus proxy;
    const bool webman = storage.Status(webmanProxy, proxy);
    WriteXmbLists(storage, xmbPlace, MakeXmbLists(xmbPlace, signedIn, webman, saves));
}

/**
 * Does what a row of the XMB asked for.
 * @param action the action the row left
 * @return what came of it, as the user reads it; empty for an action this app does not know
 */
std::string runXmbAction(Screen &screen, ConsoleStorage &storage, const std::string &profile,
                         const std::string &savedata, const SaveTarget *target, Config &config,
                         const std::string &action)
{
    if (action == xmbSignIn)
    {
        return signIn(screen, profile, config);
    }
    if (config.refreshToken.empty())
    {
        return "Sign in to Google Drive first.";
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
        return std::string();
    }
    // An import creates the folder when it is missing, so that saves can go into it.
    if (!findBackupFolder(!isSave, folderId))
    {
        return driveFailureMessage();
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
        return "There are no saves in the cloud.";
    }
    if (!browser.Refresh())
    {
        return driveFailureMessage();
    }
    return PickXmbSave(browser, syncDialog, action.substr(sizeof xmbSave - 1));
}

/**
 * The app on its own thread: finds the logged-in user, does what the XMB's row asked for, lists
 * the saves in the cloud for the XMB anew and goes back to the XMB. Started from its own icon, it
 * only lists them, or signs the user in first.
 * @param screen says what the app is doing behind the system's dialogs
 */
void runApp(Screen &screen)
{
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

    ConsoleStorage consoleStorage;
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
        const std::string result =
            runXmbAction(screen, consoleStorage, profile, savedata, saveTarget, config, action);
        if (!result.empty())
        {
            syncDialog.ShowInfo(result);
        }
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
        syncDialog.ShowInfo(targetUnknownMessage);
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
