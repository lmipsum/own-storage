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
#include <sys/systime.h>
#include <sys/file.h>
#include <io/pad.h>

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
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_thread.h>

#include "auth/ClientCredentials.h"
#include "drive/GoogleDrive.h"
#include "app/Files.h"
#include "app/Config.h"
#include "auth/GoogleDeviceAuth.h"
#include "net/HttpClient.h"
#include "app/ConsoleStorage.h"
#include "app/CloudSaveMenu.h"
#include "app/ConsoleIdentity.h"
#include "app/ConsoleSettings.h"
#include "app/Profile.h"
#include "sync/SaveBrowser.h"
#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncState.h"
#include "json/Json.h"
#include "app/DebugLog.h"
#include "app/Paths.h"
#include "ui/Menu.h"
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
// How often a wait checks whether the user has gone back.
const unsigned int backPollMicroseconds = 100000;

HttpClient http;
GoogleDeviceAuth googleAuth(http);
GoogleDrive drive(http, googleAuth, parallelUploads, MakeTransferBar());

SDL_Surface *screen = NULL;
SDL_Surface *image = NULL;
TTF_Font *Sans;

// The app thread and the main thread both use this flag.
std::atomic<int> exitapp(0);

SyncDialog syncDialog(exitapp);

const char driveFullMessage[] = "Your Google storage is full, so some saves were not backed up. "
                                "Free up space in Google Drive, then sync again.";
const char driveUnreachableMessage[] = "Cannot reach Google Drive. Check the network.";
const char driveRefusedMessage[] = "Google Drive refused a request. Try again later.";
const char signInExpiredMessage[] =
    "The Google sign-in is no longer valid. Sign out, then sign in again.";
const char localFileMessage[] =
    "Own Storage cannot write to the PS3 hard disk. Check the free space.";
const char unknownUserMessage[] = "Own Storage cannot tell which user is logged in.";
const char menuHint[] = "X: select";

// The main menu's rows.
const char signInRow[] = "Sign in to Google Drive";
const char syncRow[] = "Sync";
const char cloudSavesRow[] = "Saves in the cloud";
const char signOutRow[] = "Sign out";
const char quitRow[] = "Quit";

int xmbopen = 0;

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
    switch (status)
    {
    case SYSUTIL_EXIT_GAME:
        exitapp = 1;
        break;
    case SYSUTIL_MENU_OPEN:
        xmbopen = 1;
        break;
    case SYSUTIL_MENU_CLOSE:
        xmbopen = 0;
        break;
    }
}

SDL_Surface *Load_Image(std::string filePath)
{
    SDL_Surface *loadedImage = NULL;
    SDL_Surface *optimizedImage = NULL;
    loadedImage = IMG_Load(filePath.c_str());
    if (loadedImage != NULL)
    {
        optimizedImage = SDL_DisplayFormatAlpha(loadedImage);
        SDL_FreeSurface(loadedImage);
    }
    return optimizedImage;
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

/** Shows why the app cannot go on, and waits until the user quits. */
void waitForQuit(Menu &menu, const std::string &message)
{
    menu.ShowMessage(message);
    while (exitapp == 0 && menu.Choose("", {quitRow}, menuHint, 0) == Menu::kNone)
    {
    }
}

/**
 * Signs the user in with a code to enter on another device, and keeps the sign-in for the next
 * runs. The user may go back while the code is shown.
 * @param menu where the code is shown
 * @param profile the user's directory of the app
 * @param config receives the sign-in
 */
void signIn(Menu &menu, const std::string &profile, Config &config)
{
    menu.ShowMessage("Asking Google for a sign-in code...");
    const std::string code = googleAuth.Start();
    if (code.empty())
    {
        menu.ShowMessage("Cannot reach Google. Check the network.");
        return;
    }
    menu.ShowMessage("On a phone or computer, open " + googleAuth.VerificationUrl() +
                     " and enter " + code + "    O: cancel");
    GoogleDeviceAuth::Status status = GoogleDeviceAuth::Pending;
    while (status == GoogleDeviceAuth::Pending)
    {
        // Google asks for seconds between polls; the user should not wait that long to go back.
        for (unsigned int waited = 0;
             waited < googleAuth.PollIntervalSeconds() * microsecondsPerSecond;
             waited += backPollMicroseconds)
        {
            if (exitapp != 0 || menu.TakeBack())
            {
                menu.ShowMessage("Sign-in cancelled.");
                return;
            }
            sysUsleep(backPollMicroseconds);
        }
        status = googleAuth.Poll();
    }
    if (status != GoogleDeviceAuth::Approved)
    {
        menu.ShowMessage("The sign-in did not go through. Try again.");
        return;
    }
    config.refreshToken = googleAuth.RefreshToken();
    SaveConfig(profile, config);
    menu.ShowMessage("Signed in.");
}

/** Forgets the user's sign-in, so that the user may sign in again, with another account too. */
void signOut(Menu &menu, const std::string &profile, Config &config)
{
    config = Config();
    SaveConfig(profile, config);
    googleAuth.SetRefreshToken(std::string());
    menu.ShowMessage("Signed out.");
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

/** Brings the user's saves and their backup in line. */
void runSync(Menu &menu, ConsoleStorage &storage, const std::string &profile,
             const std::string &savedata, const SaveTarget *target)
{
    menu.ShowMessage("Syncing... Do not quit or switch off the PS3.");
    syncDialog.Open();
    std::string folderId;
    std::size_t notResigned = 0;
    bool done = findBackupFolder(true, folderId);
    if (done)
    {
        // Without a state of its own every save starts without a base; the sync works it out.
        SyncState state(stateFile(profile), stateBackup(profile));
        state.Load();
        Sync sync(drive, state, storage, syncDialog, folderId, savedata, profileMarkerSave, profile,
                  target);
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
        menu.ShowMessage(driveFailureMessage());
    }
    else if (full)
    {
        menu.ShowMessage("The sync stopped: the Google storage is full.");
    }
    else if (syncDialog.Cancelled())
    {
        menu.ShowMessage("The sync was stopped.");
    }
    else if (notResigned == 1)
    {
        menu.ShowMessage("The sync is done; a save of another user came down as it was, and the "
                         "game may refuse it.");
    }
    else if (notResigned > 1)
    {
        std::ostringstream text;
        text << "The sync is done; " << notResigned
             << " saves of other users came down as they were, and the games may refuse them.";
        menu.ShowMessage(text.str());
    }
    else
    {
        menu.ShowMessage("The sync is done.");
    }
}

/** Lets the user load or delete saves of the backup one by one. */
void browseCloudSaves(Menu &menu, ConsoleStorage &storage, const std::string &profile,
                      const std::string &savedata, const SaveTarget *target)
{
    menu.ShowMessage("Looking for the backup...");
    std::string folderId;
    if (!findBackupFolder(false, folderId))
    {
        menu.ShowMessage(driveFailureMessage());
        return;
    }
    if (folderId.empty())
    {
        menu.ShowMessage("There are no saves in the cloud.");
        return;
    }
    SyncState state(stateFile(profile), stateBackup(profile));
    state.Load();
    SaveBrowser browser(drive, state, storage, syncDialog, folderId, savedata, profileMarkerSave,
                        profile, target);
    if (!BrowseCloudSaves(menu, syncDialog, browser, ReadDateStyle()))
    {
        menu.ShowMessage(driveFailureMessage());
    }
}

/**
 * The app on its own thread: finds the logged-in user, then runs the main menu until the user
 * quits. The menu waits for the main thread's pad presses.
 * @param arg the Menu
 */
int runApp(void *arg)
{
    Menu &menu = *static_cast<Menu *>(arg);
    ClientCredentials client = LoadUserCredentials(userClientFile, "google");
    if (!client.IsComplete())
    {
        client = FetchPublishedCredentials(http, "google");
    }
    if (!client.IsComplete())
    {
        waitForQuit(menu, "Cannot reach the Own Storage configuration. Check the network.");
        exitapp = 1;
        return 0;
    }
    googleAuth.SetClient(client);

    ConsoleStorage consoleStorage;
    const std::optional<std::string> user = FindLoggedInUser(consoleStorage);
    if (!user)
    {
        waitForQuit(menu, unknownUserMessage);
        exitapp = 1;
        return 0;
    }
    const std::string profile = MakeProfileDirectory(consoleStorage, *user);
    const std::string savedata = SavedataDirectory(*user);
    // Saves of another user or console are made this user's as they come down.
    const std::optional<SaveTarget> target = ReadSaveTarget(*user);
    const SaveTarget *saveTarget = target ? &*target : nullptr;
    if (saveTarget == nullptr)
    {
        menu.ShowMessage("This PS3's or the user's ids cannot be read, so saves of other users "
                         "come down as they are.");
    }
    Config config;
    LoadConfig(profile, config);
    // The first request finds the access token missing and gets one with this.
    googleAuth.SetRefreshToken(config.refreshToken);

    std::size_t cursor = 0;
    while (exitapp == 0)
    {
        const bool signedIn = !config.refreshToken.empty();
        const std::vector<std::string> rows =
            signedIn ? std::vector<std::string>{syncRow, cloudSavesRow, signOutRow, quitRow}
                     : std::vector<std::string>{signInRow, quitRow};
        const int chosen = menu.Choose("", rows, menuHint, cursor);
        if (chosen == Menu::kNone)
        {
            continue;
        }
        cursor = static_cast<std::size_t>(chosen);
        const std::string &row = rows[cursor];
        if (row == signInRow)
        {
            signIn(menu, profile, config);
            cursor = 0;
        }
        else if (row == syncRow)
        {
            runSync(menu, consoleStorage, profile, savedata, saveTarget);
        }
        else if (row == cloudSavesRow)
        {
            browseCloudSaves(menu, consoleStorage, profile, savedata, saveTarget);
        }
        else if (row == signOutRow)
        {
            signOut(menu, profile, config);
            cursor = 0;
        }
        else
        {
            exitapp = 1;
        }
    }
    return 0;
}

// The pad buttons the menus take, as bits of what a pad holds down.
const unsigned int padUp = 1 << 0;
const unsigned int padDown = 1 << 1;
const unsigned int padLeft = 1 << 2;
const unsigned int padRight = 1 << 3;
const unsigned int padCross = 1 << 4;
const unsigned int padCircle = 1 << 5;
const unsigned int padTriangle = 1 << 6;

/** The menu buttons a pad holds down. */
unsigned int heldButtons(const padData &data)
{
    return (data.BTN_UP ? padUp : 0) | (data.BTN_DOWN ? padDown : 0) |
           (data.BTN_LEFT ? padLeft : 0) | (data.BTN_RIGHT ? padRight : 0) |
           (data.BTN_CROSS ? padCross : 0) | (data.BTN_CIRCLE ? padCircle : 0) |
           (data.BTN_TRIANGLE ? padTriangle : 0);
}

/**
 * Hands the menu every button that went down on a pad since its last report.
 * @param menu takes the presses
 * @param held what each pad held down at its last report; updated
 */
void readPads(Menu &menu, unsigned int held[MAX_PADS])
{
    static const struct
    {
        unsigned int bit;
        PadButton button;
    } presses[] = {{padUp, PadButton::Up},          {padDown, PadButton::Down},
                   {padLeft, PadButton::PageUp},    {padRight, PadButton::PageDown},
                   {padCross, PadButton::Select},   {padCircle, PadButton::Back},
                   {padTriangle, PadButton::Remove}};
    padInfo info;
    ioPadGetInfo(&info);
    for (int pad = 0; pad < MAX_PADS; ++pad)
    {
        if (!info.status[pad])
        {
            // A pad plugged in again starts with nothing held.
            held[pad] = 0;
            continue;
        }
        padData data;
        // A pad without a new report keeps what it held.
        if (ioPadGetData(pad, &data) != 0 || data.len == 0)
        {
            continue;
        }
        const unsigned int now = heldButtons(data);
        for (const auto &press : presses)
        {
            if ((now & press.bit) != 0 && (held[pad] & press.bit) == 0)
            {
                menu.Press(press.button);
            }
        }
        held[pad] = now;
    }
}

int main(int argc, char **argv)
{
    std::string imagePath;
    char FontPath[] = APP_DIR "font.ttf";

    netInitialize();
    debugInit();

    SDL_Init(SDL_INIT_EVERYTHING);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();

    videoState state;
    videoGetState(0, 0, &state);
    videoResolution res;
    videoGetResolution(state.displayMode.resolution, &res);

    debugPrintf("Resolution %u,%u\n", res.width, res.height);

    appWidth = res.width;
    appHeight = res.height;

    switch (appHeight)
    {
    case 1080:
        imagePath = APP_DIR "bg.png";
        break;
    case 720:
        imagePath = APP_DIR "bg-720.png";
        break;
    case 480:
        imagePath = APP_DIR "bg-480.png";
        break;
    case 576:
        imagePath = APP_DIR "bg-576.png";
        break;
    default:
        imagePath = APP_DIR "bg.png";
        break;
    }

    screen =
        SDL_SetVideoMode(appWidth, appHeight, 32, SDL_SWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);

    image = Load_Image(imagePath);

    Sans = TTF_OpenFont(FontPath, 20.0);

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysevent_callback, NULL);
    ioPadInit(7);
    {
        // Gone before SDL shuts down, which the screen's surfaces need.
        Screen appScreen(screen, image, Sans);
        Menu menu(appScreen, exitapp);
        SDL_Thread *thread = SDL_CreateThread(runApp, &menu);
        unsigned int held[MAX_PADS] = {};

        while (exitapp == 0)
        {
            sysUtilCheckCallback();

            if (xmbopen != 1)
            {
                readPads(menu, held);
            }
            appScreen.Draw();
            SDL_Flip(screen);
        }

        debugPrintf("The application exited...%d\n", exitapp.load());

        syncDialog.Close();

        SDL_KillThread(thread);
    }
    SDL_FreeSurface(image);
    SDL_FreeSurface(screen);
    TTF_CloseFont(Sans);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    debugClose();
    netDeinitialize();
    ioPadEnd();

    return 0;
}
