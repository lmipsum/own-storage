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
#include "app/Profile.h"
#include "sync/SaveScanner.h"
#include "sync/Sync.h"
#include "sync/SyncState.h"
#include "json/Json.h"
#include "app/DebugLog.h"
#include "app/Paths.h"
#include "ui/SyncDialog.h"
#include "ui/TransferBar.h"

const std::string appPath = APP_DIR;
// A Google client of the user's own, which takes the place of the published one.
const std::string userClientFile = APP_DIR "client.json";
// Uploads that run at once; Drive takes about a second to answer each, and these waits
// overlap. Eight went 1.5 times as fast as four, with no request refused.
const unsigned int parallelUploads = 8;
const unsigned int microsecondsPerSecond = 1000000;

HttpClient http;
GoogleDeviceAuth googleAuth(http);
GoogleDrive drive(http, googleAuth, parallelUploads, MakeTransferBar());

SDL_Event event;
SDL_Surface *screen = NULL;
SDL_Surface *image = NULL;
TTF_Font *Sans;
SDL_Color GREY = {57, 57, 57};

// The line the main loop draws over the background; the sync thread sets it through
// showMessage, because SDL is not thread-safe and only the main thread may draw.
const int messageTopPercent = 70;
SDL_mutex *messageLock = NULL;
std::string pendingMessage;
bool messageChanged = false;

// The sync thread and the main thread both use these flags.
std::atomic<int> exitapp(0);
std::atomic<int> syncing(0);

SyncDialog syncDialog(exitapp);

const char driveFullMessage[] = "Your Google storage is full, so some saves were not backed up. "
                                "Free up space in Google Drive, then run Own Storage again.";
const char driveUnreachableMessage[] = "Cannot reach Google Drive. Check the network.";
const char driveRefusedMessage[] = "Google Drive refused a request. Run Own Storage again later.";
const char localFileMessage[] =
    "Own Storage cannot write to the PS3 hard disk. Check the free space.";
const char unknownUserMessage[] = "Own Storage cannot tell which user is logged in.";

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

void draw_surface(SDL_Surface *destination, SDL_Surface *source, int x, int y)
{
    SDL_Rect offset;
    offset.x = x;
    offset.y = y;
    SDL_BlitSurface(source, NULL, destination, &offset);
}

/**
 * Hands a line of text to the main loop, which draws it over the background on its next frame.
 * Safe to call from any thread.
 * @param text the line to show in place of the previous one
 */
void showMessage(const std::string &text)
{
    SDL_LockMutex(messageLock);
    pendingMessage = text;
    messageChanged = true;
    SDL_UnlockMutex(messageLock);
}

/**
 * Redraws the background and the latest line from showMessage, if it changed. Main thread only.
 */
void drawPendingMessage()
{
    SDL_LockMutex(messageLock);
    const bool changed = messageChanged;
    const std::string text = pendingMessage;
    messageChanged = false;
    SDL_UnlockMutex(messageLock);

    if (!changed)
    {
        return;
    }
    draw_surface(screen, image, 0, 0);
    SDL_Surface *rendered = TTF_RenderText_Blended(Sans, text.c_str(), GREY);
    if (rendered == NULL)
    {
        return;
    }
    draw_surface(screen, rendered, (appWidth - rendered->w) / 2,
                 (messageTopPercent * appHeight) / 100);
    SDL_FreeSurface(rendered);
}

/**
 * Ends the sync early and tells the user why; the user quits with X.
 * @param message the reason, as the user reads it
 */
void stopSync(const char *message)
{
    syncDialog.Close();
    showMessage(message);
    syncing = 0;
}

/**
 * Ends the sync early after a failed Drive request, naming its likely cause.
 */
void stopSyncAfterFailure()
{
    switch (drive.LastFailure())
    {
    case Failure::LocalFile:
        stopSync(localFileMessage);
        break;
    case Failure::Unreachable:
        stopSync(driveUnreachableMessage);
        break;
    case Failure::Refused:
        stopSync(driveRefusedMessage);
        break;
    }
}

/**
 * Initiates the user authentication and is also responsible for getting device id
 * @return "authorization_complete" if authenticated and "not_found" if it fails
 */
std::string authenticateUser()
{
    std::string authStatus = "not_found";
    std::string usercode;

    debugPrintf("Device Authentication Function Called...\n");
    usercode = googleAuth.Start();

    debugPrintf("Your User Code is:  %s \n", usercode.c_str());

    showMessage("Your User Code is: " + usercode + "\n\n Please visit " +
                googleAuth.VerificationUrl() + " for approval");

    GoogleDeviceAuth::Status status =
        usercode.empty() ? GoogleDeviceAuth::Failed : GoogleDeviceAuth::Pending;
    while (status == GoogleDeviceAuth::Pending)
    {
        sysUsleep(googleAuth.PollIntervalSeconds() * microsecondsPerSecond);
        debugPrintf("Polling for user authentication....\n");
        status = googleAuth.Poll();
    }
    if (status == GoogleDeviceAuth::Approved)
    {
        authStatus = "authorization_complete";
    }
    return authStatus;
}

/**
 * Starts the authentication and sync process, should be the Drive class
 */
int initCloudDrive(void *arg)
{
    ClientCredentials client = LoadUserCredentials(userClientFile, "google");
    if (!client.IsComplete())
    {
        client = FetchPublishedCredentials(http, "google");
    }
    if (!client.IsComplete())
    {
        // Without a client there is no sign-in; the user reads this and quits with X.
        showMessage("Cannot reach the Own Storage configuration. Check the network.");
        return 0;
    }
    googleAuth.SetClient(client);

    ConsoleStorage consoleStorage;
    const std::optional<std::string> user = FindLoggedInUser(consoleStorage);
    if (!user)
    {
        showMessage(unknownUserMessage);
        return 0;
    }
    const std::string profile = MakeProfileDirectory(consoleStorage, *user);
    const std::string savedata = SavedataDirectory(*user);

    debugPrintf("Checking if User is Authenticated...\n");
    Config config;
    const bool signedIn = LoadConfig(profile, config) && !config.refreshToken.empty();

    showMessage("Checking if User is authenticated...");

    if (!signedIn)
    {
        debugPrintf("User is not authenticated, starting Authentication...\n");
        authenticateUser();
    }
    else
    {
        debugPrintf("Refresh token read from the config file\n");

        googleAuth.SetRefreshToken(config.refreshToken);
        if (!googleAuth.Refresh())
        {
            debugPrintf("Token Read from file was invalid, need to Re-Authenticate");
            authenticateUser();
        }
    }

    config.refreshToken = googleAuth.RefreshToken();
    SaveConfig(profile, config);
    debugPrintf("Authentication Phase Complete, checking remote root folder existence\n");

    syncing = 1;

    showMessage("Syncing...Please DO NOT Quit or Shutdown the PS3!");

    syncDialog.ShowProgress();

    // If user new but remote data exists
    Json rootResource;
    if (!drive.FindFolder(driveFolderName, "root", rootResource))
    {
        stopSyncAfterFailure();
        return 0;
    }
    if (rootResource.Has("id"))
    {
        config.rootFolderId = rootResource["id"].Str();
        debugPrintf("User has previous data on Google Drive with id %s\n",
                    config.rootFolderId.c_str());
    }
    else
    {
        debugPrintf("User has no previous data on Google Drive\n");
        Json rtId;
        rtId = drive.CreateFolder(driveFolderName, "root");

        if (rtId.Has("id"))
        {
            config.rootFolderId = rtId["id"].Str();
            debugPrintf("Created root directory on Google Drive with id %s\n",
                        config.rootFolderId.c_str());
            SaveConfig(profile, config);
        }
        else
        {
            debugPrintf("Error creating root folder on Google Drive\n");
            stopSyncAfterFailure();
            return 0;
        }
    }

    // Without a state of its own every save starts without a base; the sync works it out.
    SyncState state(profile + "/state.json", profile + "/state.backup");
    state.Load();
    Sync sync(drive, state, consoleStorage, syncDialog, config.rootFolderId, savedata,
              profileMarkerSave, profile);
    if (!sync.Run())
    {
        stopSyncAfterFailure();
        return 0;
    }

    if (drive.IsFull())
    {
        syncDialog.ShowError(driveFullMessage);
    }
    syncDialog.Close();

    syncing = 0;

    exitapp = 1;

    return 0;
}

int main(int argc, char **argv)
{
    padInfo padinfo;
    padData paddata;
    SDL_Thread *thread = NULL;

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
    draw_surface(screen, image, 0, 0);

    Sans = TTF_OpenFont(FontPath, 20.0);

    messageLock = SDL_CreateMutex();
    thread = SDL_CreateThread(initCloudDrive, NULL);

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysevent_callback, NULL);

    ioPadInit(7);

    while (exitapp == 0)
    {
        sysUtilCheckCallback();

        if (xmbopen != 1)
        {
            ioPadGetInfo(&padinfo);
            for (int i = 0; i < MAX_PADS; i++)
            {
                if (padinfo.status[i])
                {
                    ioPadGetData(i, &paddata);
                    if (paddata.BTN_CROSS)
                    {
                        debugPrintf("Quit Called\n");
                        if (syncing == 0)
                        {
                            exitapp = 1; // Quit application
                        }
                    }
                }
            }
        }
        drawPendingMessage();
        SDL_Flip(screen);
    }

    debugPrintf("The application exited...%d\n", exitapp.load());

    syncDialog.Close();

    SDL_KillThread(thread);
    SDL_DestroyMutex(messageLock);
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