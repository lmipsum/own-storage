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
#include <sstream>
#include <fstream>
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

#include <sysutil/msg.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <sysutil/save.h>

#include <net/net.h>
#include <net/netctl.h>
#include <netinet/in.h>
#include <algorithm>
#include <optional>
#include <atomic>

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_thread.h>

#include <mbedtls/md.h>

#include "auth/AuthorizedQueue.h"
#include "auth/AuthorizedSend.h"
#include "auth/ClientCredentials.h"
#include "drive/GoogleDrive.h"
#include "app/Files.h"
#include "app/Config.h"
#include "auth/GoogleDeviceAuth.h"
#include "net/HttpClient.h"
#include "app/ConsoleStorage.h"
#include "sync/SaveScanner.h"
#include "sync/SyncState.h"
#include "json/Json.h"
#include "app/DebugLog.h"
#include "app/Paths.h"

const std::string appPath = APP_DIR;
const std::string remoteResourceFile = APP_DIR "remote.json";
const std::string remoteBackup = APP_DIR "remote.backup";
// The remote.json entries changed since its last full save, one JSON object per line.
const std::string remoteJournal = APP_DIR "remote.journal";
// remote.json's title on Drive.
const std::string remoteStateTitle = "remote.json";
// A Google client of the user's own, which takes the place of the published one.
const std::string userClientFile = APP_DIR "client.json";
// Uploads that run at once; Drive takes about a second to answer each, and these waits
// overlap. Eight went 1.5 times as fast as four, with no request refused.
const unsigned int parallelUploads = 8;
const unsigned int microsecondsPerSecond = 1000000;

HttpClient http;
GoogleDeviceAuth googleAuth(http);
// The lower progress bar shows the files GoogleDrive moves; its functions are further down.
int transferProgress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
void beginTransfer(const std::string &path);
void endTransfer();
GoogleDrive drive(http, googleAuth, TransferDisplay{beginTransfer, transferProgress, endTransfer});

Config config;

// The console's hard disk, and what it holds now as the save scan found it.
const ConsoleStorage consoleStorage{};
Json::Array localEntries;
// The sync thread's view of the backup on Drive.
SyncState remoteState(remoteResourceFile, remoteBackup, remoteJournal);

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

msgType dialog_type;

// The sync thread and the main thread both use these flags.
static std::atomic<int> dialog_action(0);
std::atomic<int> exitapp(0);
std::atomic<int> syncing(0);

const char driveFullMessage[] = "Your Google storage is full, so some saves were not backed up. "
                                "Free up space in Google Drive, then run Own Storage again.";
const char driveUnreachableMessage[] = "Cannot reach Google Drive. Check the network.";
const char driveRefusedMessage[] = "Google Drive refused a request. Run Own Storage again later.";
const char localFileMessage[] =
    "Own Storage cannot write to the PS3 hard disk. Check the free space.";
const char remoteStateDamagedMessage[] =
    "The Own Storage state on Google Drive (remote.json) cannot be read.";
const unsigned int dialogPollMicroseconds = 100000;

int xmbopen = 0;

double progFuncLastProgress = 0.0;

int appWidth = 1280;
int appHeight = 720;

std::string userType = "one";

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

static void dialog_handler(msgButton button, void *usrData)
{
    switch (button)
    {
    case MSG_DIALOG_BTN_OK:
        dialog_action = 1;
        break;
    case MSG_DIALOG_BTN_NO:
    case MSG_DIALOG_BTN_ESCAPE:
        dialog_action = 2;
        break;
    case MSG_DIALOG_BTN_NONE:
        dialog_action = -1;
        break;
    default:
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

int transferProgress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    if (dltotal > 0.0)
    {
        double curProg = (dlnow / dltotal) * 100;
        if (curProg - progFuncLastProgress > 1)
        {
            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, curProg - progFuncLastProgress);

            debugPrintf("DOWN: %f of %f P: %f C: %f\r\n", dlnow, dltotal, progFuncLastProgress,
                        curProg);

            progFuncLastProgress = curProg;
        }
    }

    if (ultotal > 0.0)
    {
        double curProg = (ulnow / ultotal) * 100;
        if (curProg - progFuncLastProgress > 1)
        {
            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, curProg - progFuncLastProgress);

            debugPrintf("UP: %f of %f P: %f C: %f\r\n", ulnow, ultotal, progFuncLastProgress,
                        curProg);

            progFuncLastProgress = curProg;
        }
    }

    return 0;
}

/**
 * Shows on the lower progress bar that a file starts to move.
 * @param path the local file
 */
void beginTransfer(const std::string &path)
{
    progFuncLastProgress = 0.0;
    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, path.c_str());
}

/**
 * Fills the lower progress bar for a file that moved in one go.
 */
void endTransfer()
{
    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 100);
}

/**
 * Creates a directory locally
 * @param path the path of the directory to create
 */
void downloadDirectory(std::string path)
{
    sysFsMkdir(path.c_str(), 0755);
}

/** How reading remote.json from Drive ended. */
enum class RemoteStateFetch
{
    /** Read, or Drive has none. */
    Done,
    /** A Drive request failed. */
    Failed,
    /** Downloaded, but it is not a sync state. */
    Damaged
};

/**
 * Downloads remote.json, the state of the backup on Drive, and reads it.
 * @param state receives the state; empty when Drive has no remote.json or it cannot be read
 * @return how it ended
 */
RemoteStateFetch downloadRemoteState(Json &state)
{
    state = Json();
    Json remoteJson;
    if (!drive.FindInFolder(remoteStateTitle, ItemKind::File, config.rootFolderId, remoteJson))
    {
        return RemoteStateFetch::Failed;
    }
    if (!remoteJson.Has("id"))
    {
        debugPrintf("downloadRemoteState: there is no remote.json on Google Drive\n");
        return RemoteStateFetch::Done;
    }
    debugPrintf("Downloading remote.json from Google Drive with id %s\n",
                remoteJson["id"].Str().c_str());
    if (!drive.DownloadFile(remoteJson["id"].Str(), remoteResourceFile))
    {
        return RemoteStateFetch::Failed;
    }
    // A journal left on disk belongs to the state the download has just replaced.
    std::remove(remoteJournal.c_str());
    state = Json::Load(remoteResourceFile);
    if (!state.Has("data"))
    {
        // Left on disk, the next run would take it for its own state.
        std::remove(remoteResourceFile.c_str());
        state = Json();
        return RemoteStateFetch::Damaged;
    }
    return RemoteStateFetch::Done;
}

void downloadChanges()
{
    u32 remSize = remoteState.Size();
    u32 i = 0;
    u32 progressCounter = 0;
    u32 totalProgress = 0;
    u32 prevProgress = 0;
    // u32 globProgress = 0;

    std::string result;

    for (i = 0; i < remSize; i++)
    {
        Json resource = remoteState.Entry(i);
        if (resource["status"].Str() == syncedStatus)
        {
            sysFSStat entry;

            if (sysFsStat(resource["path"].Str().c_str(), &entry) != 0)
            {
                totalProgress++;
            }
        }
    }

    debugPrintf("downloadChanges Step 1: Creating directories first on HDD\n");

    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX0);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Downloading Parent Entries...");
    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, 0);

    for (i = 0; i < remSize; i++)
    {
        if (dialog_action != 0)
            break;

        Json resource = remoteState.Entry(i);

        if (resource["status"].Str() == syncedStatus)
        {
            if (resource["mimeType"].Str() == folderMime)
            {
                sysFSStat entry;
                result = resource["path"].Str();

                if (sysFsStat(result.c_str(), &entry) != 0)
                {
                    progressCounter++;
                    debugPrintf("downloadChanges: Creating Directory with path %s\n",
                                result.c_str());

                    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 0);
                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1,
                                               resource["path"].Str().c_str());

                    downloadDirectory(result);

                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 100);

                    std::stringstream ss;
                    ss << "Downloading Parent Entries: " << progressCounter << " of "
                       << totalProgress;

                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, ss.str().c_str());
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                            ((progressCounter * 100) / totalProgress) -
                                                prevProgress);

                    /*if(userType == "three")
                    {
                        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 100) /
                    totalProgress) - prevProgress);
                    }
                    if(userType == "two")
                    {
                        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 40) /
                    totalProgress) - globProgress);
                    }
                    globProgress = (progressCounter * 40) / totalProgress;*/
                    prevProgress = (progressCounter * 100) / totalProgress;
                }
            }
        }
    }

    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Downloading Child Entries...");

    debugPrintf("downloadChanges Step 2: Creating files on HDD\n");

    for (i = 0; i < remSize; i++)
    {
        if (dialog_action != 0)
            break;

        Json resource = remoteState.Entry(i);

        if (resource["status"].Str() == syncedStatus)
        {
            if (resource["mimeType"].Str() != folderMime)
            {
                sysFSStat entry;
                result = resource["path"].Str();

                if (sysFsStat(result.c_str(), &entry) != 0)
                {
                    progressCounter++;
                    debugPrintf("downloadChanges: Creating file with path %s\n", result.c_str());

                    if (!drive.DownloadFile(resource["id"].Str(), result))
                    {
                        continue;
                    }

                    std::stringstream ss;
                    ss << "Downloading Child Entries: " << progressCounter << " of "
                       << totalProgress;

                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, ss.str().c_str());
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                            ((progressCounter * 100) / totalProgress) -
                                                prevProgress);

                    /*if(userType == "three")
                    {
                        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 100) /
                    totalProgress) - prevProgress);
                    }
                    if(userType == "two")
                    {
                        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 40) /
                    totalProgress) - globProgress);
                    }
                    globProgress = (progressCounter * 40) / totalProgress;*/
                    prevProgress = (progressCounter * 100) / totalProgress;
                }
            }
        }
    }

    debugPrintf("downloadChanges finished, All synced files Downloaded\n");
}

/**
 * Tells the user that Drive has no room left, and waits until the message is closed.
 */
void showDriveFullDialog()
{
    dialog_action = 0;
    // msgDialogOpen2 refuses while another dialog is open, and the aborted sync dialog may
    // still be closing.
    while (msgDialogOpen2(static_cast<msgType>(MSG_DIALOG_ERROR | MSG_DIALOG_BTN_TYPE_OK),
                          driveFullMessage, dialog_handler, NULL, NULL) != 0 &&
           exitapp == 0)
    {
        sysUsleep(dialogPollMicroseconds);
    }
    while (dialog_action == 0 && exitapp == 0)
    {
        sysUsleep(dialogPollMicroseconds);
    }
}

/**
 * Ends the sync early and tells the user why; the user quits with X.
 * @param message the reason, as the user reads it
 */
void stopSync(const char *message)
{
    msgDialogAbort();
    showMessage(message);
    syncing = 0;
}

/**
 * Ends the sync early after a failed Drive request, naming its likely cause.
 */
void stopSyncAfterFailure()
{
    const long status = drive.LastFailureStatus();
    if (status == HttpClient::kLocalError)
    {
        stopSync(localFileMessage);
    }
    else
    {
        stopSync(status < 0 ? driveUnreachableMessage : driveRefusedMessage);
    }
}

/**
 * Ends the sync early when remote.json could not be read from Drive.
 * @param fetched how reading it ended; not RemoteStateFetch::Done
 */
void stopSyncAfterFetch(RemoteStateFetch fetched)
{
    if (fetched == RemoteStateFetch::Damaged)
    {
        stopSync(remoteStateDamagedMessage);
    }
    else
    {
        stopSyncAfterFailure();
    }
}

std::string calculateMD5Checksum(const std::string &path);

/**
 * Looks for the Drive item an earlier, interrupted run created for an entry.
 * @param resource the remote.json entry
 * @param parentId the Drive folder that holds the item
 * @param item receives the item; without "id" when the entry is not in creatingStatus or Drive
 * has no such item
 * @return false when Drive could not be asked, so the entry must not be created now
 */
bool findInterruptedCreation(const Json &resource, const std::string &parentId, Json &item)
{
    item = Json();
    if (resource["status"].Str() != creatingStatus)
    {
        return true;
    }
    const ItemKind kind =
        resource["mimeType"].Str() == folderMime ? ItemKind::Folder : ItemKind::File;
    if (!drive.FindInFolder(resource["title"].Str(), kind, parentId, item))
    {
        debugPrintf("findInterruptedCreation: %s left for the next run\n",
                    resource["path"].Str().c_str());
        return false;
    }
    if (item.Has("id"))
    {
        debugPrintf("findInterruptedCreation: %s is already on Drive from an interrupted run\n",
                    resource["path"].Str().c_str());
    }
    return true;
}

/**
 * The upper progress bar of an upload: how many of the pending entries are done.
 */
struct UploadProgress
{
    const char *label;
    u32 total;
    u32 done = 0;
    u32 shownPercent = 0;

    /** Counts one more finished entry and moves the bar. */
    void Advance()
    {
        ++done;
        std::stringstream text;
        text << label << ": " << done << " of " << total;
        msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, text.str().c_str());
        const u32 percent = done * 100 / total;
        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, percent - shownPercent);
        shownPercent = percent;
    }
};

/**
 * Settles a file that an interrupted run may already have stored on Drive.
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param progress the bar to move when the stored file is taken over as it is
 * @param fileId receives the Drive file to update, or "" when a new one is to be created
 * @return true when nothing is to be sent: Drive holds the file as it is now, or Drive could not
 * be asked whether it does
 */
bool adoptInterruptedFile(u32 index, const std::string &parentId, UploadProgress &progress,
                          std::string &fileId)
{
    const Json resource = remoteState.Entry(index);
    fileId = resource["status"].Str() == updateStatus ? resource["id"].Str() : std::string();
    Json found;
    if (!findInterruptedCreation(resource, parentId, found))
    {
        return true;
    }
    if (!found.Has("id"))
    {
        return false;
    }
    if (found["md5Checksum"].Str() == calculateMD5Checksum(resource["path"].Str()))
    {
        remoteState.RecordFile(index, parentId, found);
        progress.Advance();
        return true;
    }
    // The interrupted upload may not hold the file's current content.
    fileId = found["id"].Str();
    return false;
}

/**
 * Shows on the lower progress bar what the queue is sending.
 * @param running how many requests the queue is sending besides the one starting now
 * @param kind what is being sent, in the plural
 * @param title the item starting now
 */
void showRequestsInProgress(unsigned int running, const char *kind, const std::string &title)
{
    std::stringstream text;
    text << running + 1 << " " << kind << " in progress, last: " << title;
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, text.str().c_str());
}

/**
 * Builds the request that creates a folder, or takes over the folder an interrupted run created.
 * @param index the folder's entry in remote.json
 * @param parentId the Drive folder to create it in
 * @param progress the bar to move when the folder is taken over
 * @param running how many requests the queue is sending, for the lower progress bar
 * @param request receives the request
 * @return false when nothing is to be sent: the folder exists already, or Drive could not be
 * asked whether it does
 */
bool prepareFolderRequest(u32 index, const std::string &parentId, UploadProgress &progress,
                          unsigned int running, QueuedRequest &request)
{
    const Json resource = remoteState.Entry(index);
    debugPrintf("prepareFolderRequest: Syncing directory %s under %s\n",
                resource["title"].Str().c_str(), parentId.c_str());
    Json found;
    if (!findInterruptedCreation(resource, parentId, found))
    {
        return false;
    }
    if (found.Has("id"))
    {
        remoteState.RecordFolder(index, parentId, found);
        progress.Advance();
        return false;
    }
    remoteState.MarkCreating(index);
    request = drive.BuildFolderRequest(resource["title"].Str(), parentId);
    showRequestsInProgress(running, "folders", resource["title"].Str());
    return true;
}

/**
 * Saves the folder Drive created, or logs why it did not.
 * @param index the folder's entry in remote.json
 * @param parentId the Drive folder it was created in
 * @param progress the bar to move when the folder exists
 * @param status the HTTP status, or below zero when no answer came
 * @param reply Drive's answer
 */
void finishFolderRequest(u32 index, const std::string &parentId, UploadProgress &progress,
                         long status, const std::string &reply)
{
    const Json folder = Json::Parse(reply);
    if (!HttpClient::Succeeded(status) || !folder.Has("id"))
    {
        drive.NoteFailure("finishFolderRequest: Could not create directory", status, reply);
        return;
    }
    debugPrintf("finishFolderRequest: created %s\n", folder["id"].Str().c_str());
    remoteState.RecordFolder(index, parentId, folder);
    progress.Advance();
}

/**
 * Queues the creation of a folder whose parent already exists on Drive.
 * @param queue the queue that sends it
 * @param index the folder's entry in remote.json
 * @param parentId the Drive folder to create it in
 * @param progress the bar to move when it is done
 */
void queueFolder(AuthorizedQueue &queue, u32 index, const std::string &parentId,
                 UploadProgress &progress)
{
    queue.Add([&queue, index, parentId, &progress](QueuedRequest &request)
              { return prepareFolderRequest(index, parentId, progress, queue.Running(), request); },
              [index, parentId, &progress](long status, const std::string &reply)
              { finishFolderRequest(index, parentId, progress, status, reply); });
}

/**
 * Builds the request that uploads a pending or changed file, unless an interrupted run stored
 * it already or it cannot go in one request.
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param progress the bar to move when the stored file is taken over
 * @param outsideQueue collects the files that cannot go in one request
 * @param running how many requests the queue is sending, for the lower progress bar
 * @param request receives the request
 * @return false when nothing is to be sent by the queue
 */
bool prepareFileRequest(u32 index, const std::string &parentId, UploadProgress &progress,
                        std::vector<u32> &outsideQueue, unsigned int running,
                        QueuedRequest &request)
{
    const Json resource = remoteState.Entry(index);
    const std::string path = resource["path"].Str();
    const std::string title = resource["title"].Str();
    sysFSStat entry;
    if (sysFsStat(path.c_str(), &entry) != 0 || entry.st_size > multipartUploadLimit)
    {
        outsideQueue.push_back(index);
        return false;
    }
    debugPrintf("prepareFileRequest: Syncing file %s under %s\n", title.c_str(), parentId.c_str());

    std::string fileId;
    if (adoptInterruptedFile(index, parentId, progress, fileId))
    {
        return false;
    }
    std::string content;
    if (!ReadWholeFile(path, content) || content.find(multipartBoundary) != std::string::npos)
    {
        outsideQueue.push_back(index);
        return false;
    }
    if (fileId.empty())
    {
        remoteState.MarkCreating(index);
    }
    request = drive.BuildMultipartRequest(BuildMetadata(title, octetMime, parentId), octetMime,
                                          content, fileId);

    showRequestsInProgress(running, "files", title);
    return true;
}

/**
 * Saves the file Drive stored, or logs why it did not.
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param progress the bar to move when the file is stored
 * @param status the HTTP status, or below zero when no answer came
 * @param reply Drive's answer
 */
void finishFileRequest(u32 index, const std::string &parentId, UploadProgress &progress,
                       long status, const std::string &reply)
{
    const Json file = Json::Parse(reply);
    if (!HttpClient::Succeeded(status) || !file.Has("id"))
    {
        drive.NoteFailure("finishFileRequest: Could not upload file", status, reply);
        return;
    }
    debugPrintf("finishFileRequest: stored %s\n", file["id"].Str().c_str());
    remoteState.RecordFile(index, parentId, file);
    progress.Advance();
}

/**
 * Queues the upload of a pending or changed file.
 * @param queue the queue that sends it
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param progress the bar to move when it is done
 * @param outsideQueue collects the files that cannot go in one request
 */
void queueFile(AuthorizedQueue &queue, u32 index, const std::string &parentId,
               UploadProgress &progress, std::vector<u32> &outsideQueue)
{
    queue.Add(
        [&queue, index, parentId, &progress, &outsideQueue](QueuedRequest &request)
        {
            return prepareFileRequest(index, parentId, progress, outsideQueue, queue.Running(),
                                      request);
        },
        [index, parentId, &progress](long status, const std::string &reply)
        { finishFileRequest(index, parentId, progress, status, reply); });
}

/**
 * Uploads a file that cannot go in one request, the resumable way.
 * @param index the file's entry in remote.json
 * @param parentId the Drive folder that holds it
 * @param progress the bar to move when it is done
 */
void uploadFileOutsideQueue(u32 index, const std::string &parentId, UploadProgress &progress)
{
    const Json resource = remoteState.Entry(index);
    const std::string path = resource["path"].Str();
    const std::string title = resource["title"].Str();
    debugPrintf("uploadFileOutsideQueue: Syncing file %s under %s\n", title.c_str(),
                parentId.c_str());

    std::string fileId;
    if (adoptInterruptedFile(index, parentId, progress, fileId))
    {
        return;
    }
    if (fileId.empty())
    {
        remoteState.MarkCreating(index);
    }
    const Json file = drive.UploadFile(path, title, octetMime, parentId, fileId);
    if (file.Has("id"))
    {
        remoteState.RecordFile(index, parentId, file);
        progress.Advance();
    }
}

/**
 * Brings remote.json on Drive up to the local one, unless it holds the same content already.
 * Comparing the content also catches the changes of a run that ended before it pushed them.
 */
void pushRemoteState()
{
    Json stored;
    if (!drive.FindInFolder(remoteStateTitle, ItemKind::File, config.rootFolderId, stored))
    {
        debugPrintf("pushRemoteState: remote.json left for the next run\n");
        return;
    }
    if (stored["md5Checksum"].Str() == calculateMD5Checksum(remoteResourceFile))
    {
        return;
    }
    debugPrintf("pushRemoteState: Uploading remote.json to Google Drive\n");
    // Without an id this creates remote.json; when the answer is lost, the next run finds it here.
    drive.UploadFile(remoteResourceFile, remoteStateTitle, jsonMime, config.rootFolderId,
                     stored["id"].Str());
}

/**
 * Uploads everything marked in remote resource Tree
 */
void uploadChanges()
{
    const u32 remSize = remoteState.Size();
    UploadProgress progress = {"Uploading Parent Entries", 0};
    for (u32 i = 0; i < remSize; i++)
    {
        const Json resource = remoteState.Entry(i);
        if (SyncState::IsPendingCreation(resource) || resource["status"].Str() == updateStatus)
        {
            progress.total++;
        }
    }
    const AuthorizedQueue::Stop stop = [] { return dialog_action != 0 || drive.IsFull(); };
    AuthorizedQueue queue(googleAuth, parallelUploads);

    debugPrintf("uploadChanges Step 1: Creating directories first on Google Drive,%u\n",
                progress.total);
    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX0);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Uploading Parent Entries...");
    // Change detection leaves its own text and a full bar here.
    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);

    // Drive finds a file by its parent folder's id, so folders go first, level by level: each
    // round creates the folders whose parent the rounds before have created.
    std::vector<bool> queued(remSize, false);
    for (u32 added = 1; added > 0;)
    {
        added = 0;
        for (u32 i = 0; i < remSize; i++)
        {
            const Json resource = remoteState.Entry(i);
            if (queued[i] || !SyncState::IsPendingCreation(resource) ||
                resource["mimeType"].Str() != folderMime)
            {
                continue;
            }
            const std::optional<std::string> parentId =
                remoteState.ParentId(resource, config.rootFolderId);
            if (parentId)
            {
                queueFolder(queue, i, *parentId, progress);
                queued[i] = true;
                ++added;
            }
        }
        queue.Run(stop);
    }

    progress.label = "Uploading Child Entries";
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Uploading Child Entries...");
    debugPrintf("uploadChanges Step 2: Creating files on Google Drive\n");

    std::vector<std::pair<u32, std::string>> files;
    for (u32 i = 0; i < remSize; i++)
    {
        const Json resource = remoteState.Entry(i);
        if ((SyncState::IsPendingCreation(resource) || resource["status"].Str() == updateStatus) &&
            resource["mimeType"].Str() != folderMime)
        {
            const std::optional<std::string> parentId =
                remoteState.ParentId(resource, config.rootFolderId);
            if (parentId)
            {
                files.push_back(std::make_pair(i, *parentId));
            }
        }
    }
    std::vector<u32> outsideQueue;
    for (const std::pair<u32, std::string> &file : files)
    {
        queueFile(queue, file.first, file.second, progress, outsideQueue);
    }
    queue.Run(stop);
    for (const std::pair<u32, std::string> &file : files)
    {
        if (stop())
        {
            break;
        }
        if (std::find(outsideQueue.begin(), outsideQueue.end(), file.first) != outsideQueue.end())
        {
            uploadFileOutsideQueue(file.first, file.second, progress);
        }
    }

    // The upload below sends remote.json as it is on disk, so it must hold every change.
    remoteState.SaveAll();

    pushRemoteState();
}

/**
 * Hashes a file the way Drive reports md5Checksum.
 *
 * @param path File to hash.
 * @return The MD5 digest as lowercase hex, or an empty string when the file cannot be read.
 */
std::string calculateMD5Checksum(const std::string &path)
{
    unsigned char digest[16];
    if (mbedtls_md_file(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), path.c_str(), digest) != 0)
    {
        return "";
    }
    std::string hex;
    char digits[3];
    for (unsigned char byte : digest)
    {
        snprintf(digits, sizeof digits, "%02x", byte);
        hex += digits;
    }
    return hex;
}

void detectChanges()
{
    // Upload Mode only for now
    // 1 - If an entry exists in local but not in remote, add it and mark for upload
    // 2 - If an entry exists in local and in remote, check if it's changed by size (folder can be
    // ignored)

    u32 lrtSize = localEntries.size();
    u32 rrtSize = remoteState.Size();
    u32 i = 0;
    u32 j = 0;
    u32 prevProgress = 0;
    bool found;

    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Detecting Changes...");
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, "Finding file differences...");

    for (i = 0; i < lrtSize; i++)
    {
        found = false;

        if (dialog_action != 0)
        {
            break;
        }

        Json lres = localEntries[i];

        for (j = 0; j < rrtSize; j++)
        {
            Json rres = remoteState.Entry(j);

            if (lres["path"].Str() == rres["path"].Str())
            {
                found = true;
                if (lres["mimeType"].Str() != folderMime)
                {
                    if (rres["status"].Str() == syncedStatus)
                    {
                        sysFSStat lstat;
                        sysFsStat(lres["path"].Str().c_str(), &lstat);

                        time_t fileModTime = lstat.st_mtime;

                        std::string remoteTime = rres["modifiedDate"].Str().substr(0, 18);

                        struct tm remoteModTime = {0};
                        strptime(remoteTime.c_str(), "%Y-%m-%dT%H:%M:%S", &remoteModTime);

                        time_t remoteEpoch = mktime(&remoteModTime);

                        // debugPrintf("Times are %lld, %lld\n", (long long int)fileModTime, (long
                        // long int)remoteEpoch);

                        double seconds = difftime(fileModTime, remoteEpoch);
                        if (seconds > 0)
                        {
                            debugPrintf("Local file has been modified %s\n",
                                        rres["path"].Str().c_str());

                            std::string md5c = calculateMD5Checksum(lres["path"].Str());

                            if (!md5c.empty())
                            {
                                if (md5c != rres["md5Checksum"].Str())
                                {
                                    debugPrintf("Local file MD5 has been changed %s,%s,%s\n",
                                                rres["path"].Str().c_str(), md5c.c_str(),
                                                rres["md5Checksum"].Str().c_str());
                                    remoteState.MarkChanged(j);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!found)
        {
            debugPrintf("Found New Entry in Local Resource, Adding for sync %s",
                        lres["path"].Str().c_str());
            remoteState.Append(lres);
        }

        std::stringstream ss;
        ss << "Finding File Differences: " << i << " of " << lrtSize;

        msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, ss.str().c_str());
        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((i * 100) / lrtSize) - prevProgress);
        msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, ((i * 100) / lrtSize) - prevProgress);

        /*if(userType=="two")
        {
                msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((i * 20) / lrtSize) -
        globProgress);
        }*/

        // globProgress = (i * 20) / lrtSize;
        prevProgress = (i * 100) / lrtSize;
    }
    // get_free_memory();
    // debugPrintf("System Memory: %u, %u\n",meminfo.avail,meminfo.total);
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

    debugPrintf("Checking if User is Authenticated...\n");
    const bool signedIn = LoadConfig(config) && !config.refreshToken.empty();

    showMessage("Checking if User is authenticated...");

    if (!signedIn)
    {
        debugPrintf("User is not authenticated, starting Authentication...\n");
        authenticateUser();
    }
    else
    {
        userType = "two";
        debugPrintf("Refresh token read from the config file\n");

        googleAuth.SetRefreshToken(config.refreshToken);
        if (!googleAuth.Refresh())
        {
            debugPrintf("Token Read from file was invalid, need to Re-Authenticate");
            authenticateUser();
        }
    }

    config.refreshToken = googleAuth.RefreshToken();
    SaveConfig(config);
    debugPrintf("Authentication Phase Complete, checking remote root folder existence\n");

    syncing = 1;

    showMessage("Syncing...Please DO NOT Quit or Shutdown the PS3!");

    msgDialogOpen2(dialog_type, "Performing Sync...", dialog_handler, NULL, NULL);

    // If user new but remote data exists
    Json rootResource;
    if (!drive.FindInFolder(driveFolderName, ItemKind::Folder, "root", rootResource))
    {
        stopSyncAfterFailure();
        return 0;
    }
    if (rootResource.Has("id"))
    {
        config.rootFolderId = rootResource["id"].Str();
        debugPrintf("User has previous data on Google Drive with id %s\n",
                    config.rootFolderId.c_str());
        if (userType == "one")
            userType = "three";
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
            SaveConfig(config);
        }
        else
        {
            debugPrintf("Error creating root folder on Google Drive\n");
            stopSyncAfterFailure();
            return 0;
        }
    }

    localEntries = SaveScanner(consoleStorage).Scan();

    // The first run, with nothing on Drive yet: everything the scan found goes up.
    if (userType == "one")
    {
        remoteState.Reset(localEntries);

        uploadChanges();
    }

    // A later run: what changed on the console goes up, and what is missing here comes down.
    if (userType == "two")
    {
        if (!remoteState.Load())
        {
            Json downloaded;
            const RemoteStateFetch fetched = downloadRemoteState(downloaded);
            if (fetched != RemoteStateFetch::Done)
            {
                stopSyncAfterFetch(fetched);
                return 0;
            }
            remoteState.Reset(downloaded);
        }

        detectChanges();

        uploadChanges();
        downloadChanges();
    }

    // The first run on a console whose user has a backup on Drive: the backup comes down.
    if (userType == "three")
    {
        Json downloaded;
        const RemoteStateFetch fetched = downloadRemoteState(downloaded);
        if (fetched != RemoteStateFetch::Done)
        {
            stopSyncAfterFetch(fetched);
            return 0;
        }
        if (!downloaded.IsEmpty())
        {
            remoteState.Reset(downloaded);
            downloadChanges();
            debugPrintf(
                "Data from Google Drive downloaded, Please restart app to upload changes\n");
        }
    }

    msgDialogAbort();
    if (drive.IsFull())
    {
        showDriveFullDialog();
    }

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

    dialog_action = 0;
    dialog_type = static_cast<msgType>(MSG_DIALOG_MUTE_ON | MSG_DIALOG_DOUBLE_PROGRESSBAR);

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
                            dialog_action = 2;
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

    msgDialogAbort();

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