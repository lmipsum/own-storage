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
#include <atomic>

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_thread.h>

#include <mbedtls/md.h>

#include "auth/AuthorizedSend.h"
#include "auth/ClientCredentials.h"
#include "auth/GoogleDeviceAuth.h"
#include "net/HttpClient.h"
#include "json/Json.h"
#include "app/DebugLog.h"
#include "app/Paths.h"

const std::string jsonMime = "application/json";
const std::string folderMime = "application/vnd.google-apps.folder";
const std::string octetMime = "application/octet-stream";
const std::string appPath = APP_DIR;
const std::string appFile = APP_DIR "config.json";
const std::string appFileBackup = APP_DIR "config.backup";
const std::string localResourceFile = APP_DIR "local.json";
const std::string remoteResourceFile = APP_DIR "remote.json";
const std::string remoteBackup = APP_DIR "remote.backup";
// A Google client of the user's own, which takes the place of the published one.
const std::string userClientFile = APP_DIR "client.json";
const std::string queryFields =
    "items(fileSize,id,md5Checksum,mimeType,modifiedDate,parents/id,title)";
// The folder in the user's Drive that holds every backup.
const std::string driveFolderName = "Own Storage";
// remote.json status of an entry whose creation on Drive started but was not recorded.
const std::string creatingStatus = "creating";

HttpClient http;
GoogleDeviceAuth googleAuth(http);

std::string rootFolderId = "root";
std::string remoteJsonId = "root";
std::string last_sync = "0";

Json localResourceRoot;
Json remoteResourceRoot;

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

bool pathExists(std::string pathToCheck)
{
    sysFSStat entry;
    return sysFsStat(pathToCheck.c_str(), &entry) == 0;
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

/**
 * Retrieves a Resource tree of the resources under a folder in google drive
 * @param parentId the parentid for whomm the resources are to be listed
 * @return the resource array containing all children
 */
Json::Array getResourcesUnderFolder(std::string parentId)
{
    long ret;
    Json::Array parsedArray;

    HttpClient::Headers hdr;

    std::string query = parentId + "' in parents";
    std::string resp;

    ret = SendAuthorized(googleAuth, hdr,
                         [&](const HttpClient::Headers &headers)
                         {
                             return http.Get("https://www.googleapis.com/drive/v2/files?q=" + query,
                                             headers, resp);
                         });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("getResourcesUnderFolder: HTTP Server Error...%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
    }
    else
    {
        Json parsedResp = Json::Parse(resp);
        parsedArray = parsedResp["items"].AsArray();
    }
    return parsedArray;
}

/**
 * The function checks if a resource exists on google drive and returns the item
 * @param resourceTitle the title of the resource to find
 * @param mimeType the mimetype of the resource
 * @param parentId the parentid of the resource's parent (if you don't know this, this function is
 * useless, unless you're searching under root)
 * @return the resource object, otherwise null
 */
Json checkIfRemoteResourceExists(std::string resourceTitle, std::string mimeType,
                                 std::string parentId)
{
    long ret;
    Json resourceObject;

    HttpClient::Headers hdr;

    std::string query = "mimeType='" + mimeType + "' and trashed=false and title='" +
                        resourceTitle + "' and '" + parentId + "' in parents";
    std::string resp;

    std::string url = "https://www.googleapis.com/drive/v2/files?q=" + http.Escape(query) +
                      "&fields=" + http.Escape(queryFields);

    ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                         { return http.Get(url, headers, resp); });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("checkIfRemoteResourceExists: HTTP Server Error...%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
    }
    else
    {
        Json parsedResp = Json::Parse(resp);
        Json::Array parsedArray = parsedResp["items"].AsArray();
        if (parsedArray.size() > 0)
        {
            resourceObject = parsedArray.at(0);
        }
    }

    return resourceObject;
}

/**
 * Stores the current application configuration in the application config json
 */
void storeConfig()
{
    Json appConfig;
    appConfig.Add("refresh_token", Json(googleAuth.RefreshToken()));
    appConfig.Add("root_id", Json(rootFolderId));
    appConfig.Add("last_sync", Json(last_sync));
    appConfig.Add("remote_fid", Json(remoteJsonId));

    appConfig.Save(appFile);
    appConfig.Save(appFileBackup);

    sysFsChmod(appFile.c_str(), 0644);
    sysFsChmod(appFileBackup.c_str(), 0644);
}

/**
 * Utility function that reads the config file for refresh token etc
 * @return "not_found" if file not found or corrupt else the refresh_token
 */
std::string isUserAuthenticated()
{
    std::string result = "not_found";
    sysFSStat entry;

    if (sysFsStat(appFile.c_str(), &entry) == 0)
    {
        debugPrintf("Config file found, parsing...\n");

        Json config = Json::Load(appFile);

        if (!config.IsEmpty())
        {
            debugPrintf("Config file not empty, returning refresh token...\n");
            result = config["refresh_token"].Str();
            rootFolderId = config.Has("root_id") ? config["root_id"].Str() : "root";
            last_sync = config.Has("last_sync") ? config["last_sync"].Str() : "0";
            remoteJsonId = config.Has("remote_fid") ? config["remote_fid"].Str() : "root";
        }
    }
    else
    {
        debugPrintf("Config file not found, returning empty handed...\n");
    }

    return result;
}

/**
 * Utility function to create meta data when a resource needs to be created on google drive
 * @param title the title of the resource
 * @param mimeType the mimetype. folder, octet etc
 * @param parentid the parentid of the resource, default is root
 * @return the json metadata in string notation
 */
std::string constructMetaData(std::string title, std::string mimeType, std::string parentid)
{
    Json parent;
    parent.Add("id", Json(parentid));

    std::vector<Json> array;
    array.push_back(parent);

    Json meta;
    meta.Add("title", Json(title));
    meta.Add("parents", Json(array));
    meta.Add("mimeType", Json(mimeType));

    return meta.Str();
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
 * Creates a directory locally
 * @param path the path of the directory to create
 */
void downloadDirectory(std::string path)
{
    sysFsMkdir(path.c_str(), 0755);
}

/**
 * Downloads a file from Google drive to local folder
 * @param localPath the local path to save the file to
 * @param fileId the id of the file in google drive to download
 */
bool downloadFile(std::string localPath, std::string fileId)
{
    long ret;

    HttpClient::Headers hdr;

    std::string resp;

    ret = SendAuthorized(googleAuth, hdr,
                         [&](const HttpClient::Headers &headers)
                         {
                             return http.Get("https://www.googleapis.com/drive/v2/files/" + fileId +
                                                 "?fields=downloadUrl",
                                             headers, resp);
                         });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("downloadFile: Could not get downloadUrl...%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return false;
    }

    // the content upload URL is in the "Location" HTTP header
    Json fileRes = Json::Parse(resp);
    std::string downloadLink;

    if (fileRes.Has("downloadUrl"))
    {
        downloadLink = fileRes["downloadUrl"].Str();
    }
    else
    {
        return false;
    }

    debugPrintf("Downlink is %s\n", downloadLink.c_str());

    progFuncLastProgress = 0.0;

    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, localPath.c_str());

    ret = SendAuthorized(
        googleAuth, hdr, [&](const HttpClient::Headers &headers)
        { return http.Download(downloadLink, headers, localPath, transferProgress); });

    debugPrintf("Return Code for file download is %d\n", ret);

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("downloadFile: Could not download File..%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return false;
    }

    return true;
}

void downloadChanges()
{
    u32 remSize = remoteResourceRoot["data"].AsArray().size();
    u32 i = 0;
    u32 progressCounter = 0;
    u32 totalProgress = 0;
    u32 prevProgress = 0;
    // u32 globProgress = 0;

    std::string result;

    for (i = 0; i < remSize; i++)
    {
        Json resource = remoteResourceRoot["data"][i];
        if (resource["status"].Str() == "synced")
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

        Json resource = remoteResourceRoot["data"][i];

        if (resource["status"].Str() == "synced")
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

        Json resource = remoteResourceRoot["data"][i];

        if (resource["status"].Str() == "synced")
        {
            if (resource["mimeType"].Str() != folderMime)
            {
                sysFSStat entry;
                result = resource["path"].Str();

                if (sysFsStat(result.c_str(), &entry) != 0)
                {
                    progressCounter++;
                    debugPrintf("downloadChanges: Creating file with path %s\n", result.c_str());

                    if (!downloadFile(result, resource["id"].Str()))
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
 * Uploads a file to Google Drive
 * @param path the local file path
 * @param filename the name of the file with extension
 * @param mimeType the mimetype of the file, default is octet
 * @param parentId the parent id in drive of the file
 * @return the created resource in Json format
 */
Json uploadFile(std::string path, std::string filename, std::string mimeType, std::string parentId,
                std::string fileId = "")
{
    long ret;
    Json obj;
    sysFSStat entry;

    if (sysFsStat(path.c_str(), &entry) != 0)
    {
        debugPrintf("uploadFile: Could not open file, another ps3?\n");
        return obj;
    }

    std::ostringstream xcontent_len;
    xcontent_len << "X-Upload-Content-Length: " << entry.st_size;

    HttpClient::Headers hdr;
    hdr.push_back("Content-Type: application/json");
    hdr.push_back("X-Upload-Content-Type: " + mimeType);
    hdr.push_back(xcontent_len.str());
    // hdr.push_back( "If-Match: " + m_etag ) ;
    hdr.push_back("Expect:");

    std::string meta = constructMetaData(filename, mimeType, parentId);

    std::string resp;

    std::string postUrl;

    if (fileId == "")
    {
        postUrl = "https://www.googleapis.com/upload/drive/v2/files?uploadType=resumable&fields=" +
                  http.Escape("fileSize,id,md5Checksum,modifiedDate,parents/id");
        ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                             { return http.Post(postUrl, meta, headers, resp); });
    }
    else
    {
        postUrl = "https://www.googleapis.com/upload/drive/v2/files/" + fileId +
                  "?uploadType=resumable&fields=" +
                  http.Escape("fileSize,id,md5Checksum,modifiedDate,parents/id");
        ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                             { return http.Put(postUrl, meta, headers, resp); });
    }

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("uploadFile: Could not pose meta data..%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return obj;
    }

    std::ostringstream content_len;
    content_len << "Content-Length: " << entry.st_size;

    HttpClient::Headers uphdr;
    uphdr.push_back("Content-Type: " + mimeType);
    uphdr.push_back(content_len.str());
    uphdr.push_back("Expect:");
    uphdr.push_back("Accept:");
    uphdr.push_back("Accept-Encoding: gzip");

    // the content upload URL is in the "Location" HTTP header
    const std::string uploadUrl = http.Location();
    std::string resp2;

    progFuncLastProgress = 0.0;

    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, path.c_str());

    ret =
        SendAuthorized(googleAuth, uphdr, [&](const HttpClient::Headers &headers)
                       { return http.Upload(uploadUrl, headers, path, transferProgress, resp2); });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("uploadFile: Could not upload file..%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return obj;
    }

    obj = Json::Parse(resp2);
    if (obj.Has("id"))
    {
        debugPrintf("Resource id is %s\n", obj["id"].Str().c_str());
    }
    else
    {
        debugPrintf("Resource id not found");
    }

    return obj;
}

/**
 * Creates a directory on the google drive
 * @param dirName the name to give the directory
 * @param parentId the parentid of this directory, default is root
 * @return the created folder resource in Json format
 */
Json uploadDirectory(std::string dirName, std::string parentId)
{
    long ret = -1;
    Json obj;

    // Step 1 - Creating SaveData Directory Online
    HttpClient::Headers hdr;
    hdr.push_back("Content-Type: application/json");
    hdr.push_back("Expect:");

    std::string meta = constructMetaData(dirName, folderMime, parentId);

    std::string resp;

    debugPrintf("Attempting folder creation on GDrive...\n");

    ret = SendAuthorized(googleAuth, hdr,
                         [&](const HttpClient::Headers &headers)
                         {
                             return http.Post("https://www.googleapis.com/drive/v2/files?fields=" +
                                                  http.Escape("id,modifiedDate,parents/id"),
                                              meta, headers, resp);
                         });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("uploadDirectory: Could not upload directory..%l\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return obj;
    }

    obj = Json::Parse(resp);
    if (obj.Has("id"))
    {
        debugPrintf("Folder id is %d, %s\n", ret, obj["id"].Str().c_str());
    }
    else
    {
        debugPrintf("Folder creation failed with status %d\n", ret);
    }

    return obj;
}

std::string getParentIdByFolderName(std::string folderName)
{
    std::string result = "not_found";
    if (folderName == driveFolderName)
    {
        result = rootFolderId;
    }
    else
    {
        int remSize = remoteResourceRoot["data"].AsArray().size();
        for (int i = 0; i < remSize; i++)
        {
            Json resource = remoteResourceRoot["data"][i];
            if (resource["mimeType"].Str() == folderMime)
            {
                if (resource["title"].Str() == folderName)
                {
                    result = resource["id"].Str();
                }
            }
        }
    }

    debugPrintf("getParentIdByFolderName: Returning Parent id %s for folder %s\n", result.c_str(),
                folderName.c_str());

    return result;
}

void writeChangesToFile()
{
    // Backup remote.json before a write to save from any catastrophies
    remoteResourceRoot.Save(remoteBackup);
    sysFsChmod(remoteBackup.c_str(), 0644);

    remoteResourceRoot.Save(remoteResourceFile);
    sysFsChmod(remoteResourceFile.c_str(), 0644);

    debugPrintf("writeChangesToFile: Changes to remote.json written successfully\n");
}

std::string calculateMD5Checksum(const std::string &path);

/**
 * Whether an entry still has to be created on Drive, including one whose creation an earlier
 * run started but did not record.
 * @param resource the remote.json entry
 * @return true when its status is "upload" or creatingStatus
 */
bool isPendingCreation(const Json &resource)
{
    const std::string status = resource["status"].Str();
    return status == "upload" || status == creatingStatus;
}

/**
 * Saves that an entry's creation on Drive is about to start. A run that ends before it records
 * the answer leaves the entry in creatingStatus, so the next run looks for the item before
 * creating it again.
 * @param index the entry's position in remote.json
 */
void markCreating(u32 index)
{
    remoteResourceRoot["data"][index].Add("status", Json(creatingStatus));
    writeChangesToFile();
}

/**
 * The Drive item an earlier, interrupted run created for an entry.
 * @param resource the remote.json entry
 * @param mimeType the item's type on Drive
 * @param parentId the Drive folder that holds the item
 * @return the item; without "id" when the entry is not in creatingStatus or Drive has no such item
 */
Json findInterruptedCreation(const Json &resource, const std::string &mimeType,
                             const std::string &parentId)
{
    if (resource["status"].Str() != creatingStatus)
    {
        return Json();
    }
    Json item = checkIfRemoteResourceExists(resource["title"].Str(), mimeType, parentId);
    if (item.Has("id"))
    {
        debugPrintf("uploadChanges: %s is already on Drive from an interrupted run\n",
                    resource["path"].Str().c_str());
    }
    return item;
}

/**
 * Uploads everything marked in remote resource Tree
 */
void uploadChanges()
{
    // First make sure that all folders are uploaded, we need this because file
    // resources in Google Drive don't have paths, instead they rely on
    // parent ids
    u32 remSize = remoteResourceRoot["data"].AsArray().size();
    u32 i = 0;
    u32 syncJson = 0;
    u32 progressCounter = 0;
    u32 totalProgress = 0;
    u32 prevProgress = 0;
    // u32 globProgress = 0;
    std::string result;

    for (i = 0; i < remSize; i++)
    {
        Json resource = remoteResourceRoot["data"][i];
        if (isPendingCreation(resource) || resource["status"].Str() == "update")
        {
            totalProgress++;
        }
    }

    debugPrintf("uploadChanges Step 1: Creating directories first on Google Drive,%d\n",
                totalProgress);

    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX0);

    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Uploading Parent Entries...");

    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, 0);

    for (i = 0; i < remSize; i++)
    {
        if (dialog_action != 0)
        {
            syncJson = 0;
            break;
        }

        Json resource = remoteResourceRoot["data"][i];

        if (isPendingCreation(resource))
        {
            if (resource["mimeType"].Str() == folderMime)
            {
                result = getParentIdByFolderName(resource["parentFolder"].Str());

                if (result != "not_found")
                {
                    debugPrintf("uploadChanges: Creating Directory with name %s under %s\n",
                                resource["title"].Str().c_str(), result.c_str());

                    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 0);
                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1,
                                               resource["path"].Str().c_str());

                    Json obj = findInterruptedCreation(resource, folderMime, result);
                    if (!obj.Has("id"))
                    {
                        markCreating(i);
                        obj = uploadDirectory(resource["title"].Str(), result);
                    }

                    if (!obj.Has("id"))
                    {
                        continue;
                    }

                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 100);

                    remoteResourceRoot["data"][i].Add("id", Json(obj["id"].Str()));
                    remoteResourceRoot["data"][i].Add("parentid", Json(result));
                    remoteResourceRoot["data"][i].Add("modifiedDate",
                                                      Json(obj["modifiedDate"].Str()));
                    remoteResourceRoot["data"][i].Add("status", Json("synced"));
                    writeChangesToFile();

                    syncJson = 1;
                    progressCounter++;

                    std::stringstream ss;
                    ss << "Uploading Parent Entries: " << progressCounter << " of "
                       << totalProgress;

                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, ss.str().c_str());
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                            ((progressCounter * 100) / totalProgress) -
                                                prevProgress);

                    /*if(userType == "one")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 100)
                    / totalProgress) - prevProgress);
                    }
                    if(userType == "two")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 40)
                    / totalProgress) - globProgress);
                    }
                    globProgress = (progressCounter * 40) / totalProgress;*/
                    prevProgress = (progressCounter * 100) / totalProgress;
                }
            }
        }
    }

    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, "Uploading Child Entries...");

    debugPrintf("uploadChanges Step 2: Creating files on Google Drive\n");

    for (i = 0; i < remSize; i++)
    {
        if (dialog_action != 0)
        {
            break;
        }

        Json resource = remoteResourceRoot["data"][i];

        if (isPendingCreation(resource))
        {
            if (resource["mimeType"].Str() != folderMime)
            {
                result = getParentIdByFolderName(resource["parentFolder"].Str());

                if (result != "not_found")
                {
                    debugPrintf("uploadChanges: Creating file with name %s under %s\n",
                                resource["title"].Str().c_str(), result.c_str());

                    const std::string path = resource["path"].Str();
                    Json obj = findInterruptedCreation(resource, octetMime, result);
                    if (!obj.Has("id"))
                    {
                        markCreating(i);
                        obj = uploadFile(path, resource["title"].Str(), octetMime, result);
                    }
                    else if (obj["md5Checksum"].Str() != calculateMD5Checksum(path))
                    {
                        // The interrupted upload may not hold the file's current content.
                        obj = uploadFile(path, resource["title"].Str(), octetMime, result,
                                         obj["id"].Str());
                    }

                    if (!obj.Has("id"))
                    {
                        continue;
                    }

                    remoteResourceRoot["data"][i].Add("id", Json(obj["id"].Str()));
                    remoteResourceRoot["data"][i].Add("parentid", Json(result));
                    remoteResourceRoot["data"][i].Add("modifiedDate",
                                                      Json(obj["modifiedDate"].Str()));
                    remoteResourceRoot["data"][i].Add("fileSize", Json(obj["fileSize"].Str()));
                    remoteResourceRoot["data"][i].Add("md5Checksum",
                                                      Json(obj["md5Checksum"].Str()));
                    remoteResourceRoot["data"][i].Add("status", Json("synced"));

                    writeChangesToFile();
                    syncJson = 1;
                    progressCounter++;

                    std::stringstream ss;
                    ss << "Uploading Child Entries: " << progressCounter << " of " << totalProgress;

                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, ss.str().c_str());
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                            ((progressCounter * 100) / totalProgress) -
                                                prevProgress);

                    /*if(userType == "one")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 100)
                    / totalProgress) - prevProgress);
                    }
                    if(userType == "two")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 40)
                    / totalProgress) - globProgress);
                    }
                    globProgress = (progressCounter * 40) / totalProgress;*/
                    prevProgress = (progressCounter * 100) / totalProgress;

                    // get_free_memory();
                    // debugPrintf("System Memory: %u, %u\n",meminfo.avail,meminfo.total);
                }
                // else mark this file to be deleted, if this feature is ever required
            }
        }
        else if (resource["status"].Str() == "update")
        {
            if (resource["mimeType"].Str() != folderMime)
            {
                result = getParentIdByFolderName(resource["parentFolder"].Str());

                if (result != "not_found")
                {
                    debugPrintf("uploadChanges: Re-visioning file with name %s under %s\n",
                                resource["title"].Str().c_str(), result.c_str());

                    Json obj = uploadFile(resource["path"].Str(), resource["title"].Str(),
                                          octetMime, result, resource["id"].Str());

                    if (!obj.Has("id"))
                    {
                        continue;
                    }

                    remoteResourceRoot["data"][i].Add("modifiedDate",
                                                      Json(obj["modifiedDate"].Str()));
                    remoteResourceRoot["data"][i].Add("fileSize", Json(obj["fileSize"].Str()));
                    remoteResourceRoot["data"][i].Add("md5Checksum",
                                                      Json(obj["md5Checksum"].Str()));
                    remoteResourceRoot["data"][i].Add("status", Json("synced"));

                    writeChangesToFile();
                    syncJson = 1;
                    progressCounter++;

                    std::stringstream ss;
                    ss << "Uploading Child Entries: " << progressCounter << " of " << totalProgress;

                    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX0, ss.str().c_str());
                    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0,
                                            ((progressCounter * 100) / totalProgress) -
                                                prevProgress);

                    /*if(userType == "one")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 100)
                    / totalProgress) - prevProgress);
                    }
                    if(userType == "two")
                    {
                            msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX0, ((progressCounter * 40)
                    / totalProgress) - globProgress);
                    }
                    globProgress = (progressCounter * 40) / totalProgress;*/
                    prevProgress = (progressCounter * 100) / totalProgress;

                    // get_free_memory();
                    // debugPrintf("System Memory: %u, %u\n",meminfo.avail,meminfo.total);
                }
            }
        }
    }

    debugPrintf("uploadChanges Step 3: Uploading remote.json to google drive\n");

    if (remoteJsonId == "root")
    {
        Json remJson;
        remJson = uploadFile(remoteResourceFile, "remote.json", jsonMime, rootFolderId);
        if (remJson.Has("id"))
        {
            remoteJsonId = remJson["id"].Str();
            storeConfig();
        }
    }
    else if (syncJson == 1)
    {
        debugPrintf("Updating remote.json on the server...\n");
        uploadFile(remoteResourceFile, "remote.json", jsonMime, rootFolderId, remoteJsonId);
    }
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

    u32 lrtSize = localResourceRoot["data"].AsArray().size();
    u32 rrtSize = remoteResourceRoot.Has("data") ? remoteResourceRoot["data"].AsArray().size() : 0;
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

        Json lres = localResourceRoot["data"][i];

        for (j = 0; j < rrtSize; j++)
        {
            Json rres = remoteResourceRoot["data"][j];

            if (lres["path"].Str() == rres["path"].Str())
            {
                found = true;
                if (lres["mimeType"].Str() != folderMime)
                {
                    if (rres["status"].Str() == "synced")
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
                                    remoteResourceRoot["data"][j].Add("status", Json("update"));
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
            remoteResourceRoot["data"].Append(lres);
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

    u64 epochTime;
    u64 nsec;
    u64 previousCallTime;

    sysGetCurrentTime(&epochTime, &nsec);
    previousCallTime = epochTime;

    GoogleDeviceAuth::Status status =
        usercode.empty() ? GoogleDeviceAuth::Failed : GoogleDeviceAuth::Pending;
    while (status == GoogleDeviceAuth::Pending)
    {
        sysGetCurrentTime(&epochTime, &nsec);
        if (epochTime - previousCallTime > 10)
        {
            debugPrintf("Polling for user authentication....\n");
            status = googleAuth.Poll();
            previousCallTime = epochTime;
        }
    }
    if (status == GoogleDeviceAuth::Approved)
    {
        authStatus = "authorization_complete";
    }
    return authStatus;
}

/**
 * Builds a remote Resource Tree from root folder to every file there is
 */
void buildRemoteResourceTree()
{
    std::vector<Json> resourceTree;
    std::vector<std::string> foldersToTraverse;
    Json resourceObject = checkIfRemoteResourceExists(driveFolderName, folderMime, rootFolderId);
    if (resourceObject.Has("id"))
    {
        resourceTree.push_back(Json(resourceObject));
        foldersToTraverse.push_back(resourceObject["id"].Str());
        while (foldersToTraverse.size() > 0)
        {
            Json::Array tempArray = getResourcesUnderFolder(foldersToTraverse.back());
            if (!tempArray.empty())
            {
                for (Json::Array::iterator i = tempArray.begin(); i != tempArray.end(); ++i)
                {
                    if ((*i)["mimeType"].Str() == folderMime)
                        foldersToTraverse.push_back((*i)["id"].Str());
                    resourceTree.push_back(*i);
                }
            }
            foldersToTraverse.pop_back();
        }
    }

    debugPrintf("Remote Resource Tree built, writing to Remote Resource Json\n");

    remoteResourceRoot.Add("data", Json(resourceTree));

    writeChangesToFile();
}

/**
 * Builds a local resource tree given the path of a local profile folder
 */
void buildLocalResourceTree()
{
    std::vector<Json> resourceTree;

    debugPrintf("Building Resource Tree....\n");

    sysFSDirent entry;
    s32 fd;
    u64 read;
    std::vector<std::string> profileList;
    std::vector<std::string> profilePaths;

    std::string entryName = "";
    std::string path = "/dev_hdd0/home";

    // Check how many profiles exists in user's HDD 0000001 etc
    if (sysFsOpendir(path.c_str(), &fd) == 0)
    {
        while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
        {
            if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
            {
                continue;
            }
            else
            {
                entryName.clear();
                entryName.assign(entry.d_name, strlen(entry.d_name));

                profileList.push_back(entryName);
                profilePaths.push_back(path + "/" + entryName + "/savedata");

                Json resource;
                resource.Add("id", Json("nill"));
                resource.Add("title", Json(entryName));
                resource.Add("path", Json(path + "/" + entryName + "/savedata"));
                resource.Add("mimeType", Json(folderMime));
                resource.Add("parentid", Json(rootFolderId));
                resource.Add("parentFolder", Json(driveFolderName));
                resource.Add("modifiedDate", Json("0"));
                resource.Add("status", Json("upload"));

                resourceTree.push_back(Json(resource));
            }
        }
        sysFsClosedir(fd);
        fd = -1;
    }

    std::vector<std::string>::const_iterator k = profileList.begin();
    std::vector<std::string>::const_iterator l = profilePaths.begin();
    std::vector<std::string> folderList;
    std::vector<std::string> pathList;

    // Recursively list all saves under every profile
    while (k != profileList.end())
    {
        debugPrintf("Entering Save Path...%s\n", l->c_str());

        if (sysFsOpendir(l->c_str(), &fd) == 0)
        {
            while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
            {
                if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                {
                    continue;
                }
                else
                {
                    folderList.push_back(entry.d_name);

                    entryName.clear();
                    entryName.assign(entry.d_name, strlen(entry.d_name));

                    std::stringstream fPath;
                    fPath << *l << "/" << entryName;

                    pathList.push_back(fPath.str());

                    // debugPrintf("Save Path Entry...%s\n",entryName.c_str());

                    Json resource;
                    resource.Add("id", Json("nill"));
                    resource.Add("title", Json(entryName));
                    resource.Add("path", Json(fPath.str()));
                    resource.Add("mimeType", Json(folderMime));
                    resource.Add("parentid", Json("nill"));
                    resource.Add("parentFolder", Json(k->c_str()));
                    resource.Add("modifiedDate", Json("0"));
                    resource.Add("status", Json("upload"));

                    resourceTree.push_back(Json(resource));
                }
            }
            sysFsClosedir(fd);
            fd = -1;
        }
        ++k, ++l;
    }

    debugPrintf("Directory enumeration complete...listing files inside them...\n");

    std::vector<std::string>::const_iterator i = pathList.begin();
    std::vector<std::string>::const_iterator j = folderList.begin();

    // Recursively list all files under every save directory
    while (i != pathList.end())
    {
        if (sysFsOpendir(i->c_str(), &fd) == 0)
        {
            while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
            {
                if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                {
                    continue;
                }
                else
                {
                    entryName.clear();
                    entryName.assign(entry.d_name, strlen(entry.d_name));

                    std::stringstream fPath;
                    fPath << *i << "/" << entryName;

                    // debugPrintf("Entry Path...%s\n",fPath.str().c_str());

                    Json resource;
                    resource.Add("id", Json("nill"));
                    resource.Add("title", Json(entryName));
                    resource.Add("path", Json(fPath.str()));
                    resource.Add("mimeType", Json(octetMime));
                    resource.Add("parentFolder", Json(j->c_str()));
                    resource.Add("parentid", Json("nill"));
                    resource.Add("modifiedDate", Json("0"));
                    resource.Add("fileSize", Json("0"));
                    resource.Add("md5Checksum", Json("0"));
                    resource.Add("status", Json("upload"));

                    resourceTree.push_back(Json(resource));
                }
            }
            sysFsClosedir(fd);
            fd = -1;
        }
        ++i;
        ++j;
    }

    // Check for Existence of PS1 and PS2 memory cards
    if (pathExists("/dev_hdd0/savedata"))
    {
        if (pathExists("/dev_hdd0/savedata/vmc"))
        {
            if (sysFsOpendir("/dev_hdd0/savedata/vmc", &fd) == 0)
            {
                while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
                {
                    if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                    {
                        continue;
                    }
                    else
                    {
                        entryName.clear();
                        entryName.assign(entry.d_name, strlen(entry.d_name));

                        debugPrintf("Memcard%s\n", entryName.c_str());

                        Json resource;
                        resource.Add("id", Json("nill"));
                        resource.Add("title", Json(entryName));
                        resource.Add("path", Json("/dev_hdd0/savedata/vmc/" + entryName));
                        resource.Add("mimeType", Json(octetMime));
                        resource.Add("parentid", Json(rootFolderId));
                        resource.Add("parentFolder", Json(driveFolderName));
                        resource.Add("modifiedDate", Json("0"));
                        resource.Add("status", Json("upload"));

                        resourceTree.push_back(Json(resource));
                    }
                }
                sysFsClosedir(fd);
                fd = -1;
            }
        }
    }

    debugPrintf("Resource Tree built, writing to Resource Json\n");

    // Overwrite local.json every time
    localResourceRoot.Add("data", Json(resourceTree));
    localResourceRoot.Save(localResourceFile);
    sysFsChmod(localResourceFile.c_str(), 0644);

    debugPrintf("Resource file has been written successfully...\n");
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
    std::string authStatus = isUserAuthenticated();

    showMessage("Checking if User is authenticated...");

    if (authStatus == "not_found")
    {
        debugPrintf("User is not authenticated, starting Authentication...\n");
        authenticateUser();
    }
    else
    {
        userType = "two";
        debugPrintf("Refresh token read from the config file\n");

        googleAuth.SetRefreshToken(authStatus);
        if (!googleAuth.Refresh())
        {
            debugPrintf("Token Read from file was invalid, need to Re-Authenticate");
            authenticateUser();
        }
    }

    storeConfig();
    debugPrintf("Authentication Phase Complete, checking remote root folder existence\n");

    syncing = 1;

    showMessage("Syncing...Please DO NOT Quit or Shutdown the PS3!");

    msgDialogOpen2(dialog_type, "Performing Sync...", dialog_handler, NULL, NULL);

    // If user new but remote data exists
    Json rootResource = checkIfRemoteResourceExists(driveFolderName, folderMime, "root");
    if (rootResource.Has("id"))
    {
        rootFolderId = rootResource["id"].Str();
        debugPrintf("User has previous data on Google Drive with id %s\n", rootFolderId.c_str());
        if (userType == "one")
            userType = "three";
    }
    else
    {
        debugPrintf("User has no previous data on Google Drive\n");
        Json rtId;
        rtId = uploadDirectory(driveFolderName, "root");

        if (rtId.Has("id"))
        {
            rootFolderId = rtId["id"].Str();
            debugPrintf("Created root directory on Google Drive with id %s\n",
                        rootFolderId.c_str());
            storeConfig();
        }
        else
        {
            debugPrintf("Error creating root folder on Google Drive\n");
        }
    }

    buildLocalResourceTree();

    // NOTE: LRT = Local Resource Tree and RRT= Remote Resource Tree

    // First Time: App never run AND no data on Google Drive
    // DONE: Build Local Resource Tree and write to local.json
    // DONE: Copy Local Resource Tree to Remote Resource Tree
    // DONE: Write remote.json
    // DONE: Upload all entries in remote.json to Google Drive
    // DONE: Update remote.json with all changes during uploading
    // DONE: Write remote.json to hard drive during changes
    // DONE: Upload remote.json to Google Drive
    if (userType == "one")
    {
        remoteResourceRoot = Json::Load(localResourceFile);
        remoteResourceRoot.Save(remoteResourceFile);

        uploadChanges();
    }

    // Second Time: App ran before AND data possibly exists on Google Drive
    // DONE: Build Local Resource Tree and write to local.json
    // DONE: Read Remote Resource Tree
    // TODO: Go through all entries in LRT and search for them in RRT
    // TODO: If any entry does not exist in RRT, insert it into RRT
    // TODO: If any entry exists in RRT, match size, if size different mark as dirty
    // TODO: Upload all changes marked as dirty in RRT to Google Drive
    // TODO: Write all changes or uploads back to RRT and save to HDD
    // TODO: Write RRT to HDD
    // TODO: Upload RRT to Google Drive
    if (userType == "two")
    {
        remoteResourceRoot = Json::Load(remoteResourceFile);

        detectChanges();

        uploadChanges();
        downloadChanges();
    }

    // First Time: App is installed on a new system but Data exists on Google Drive
    // TODO: Build LRT and write to local.json
    // TODO: Download Remote Resource Tree and write to remote.json
    // TODO: Revert to Second Time scenario
    if (userType == "three")
    {
        Json remoteJson = checkIfRemoteResourceExists("remote.json", jsonMime, rootFolderId);

        if (remoteJson.Has("id"))
        {
            remoteJsonId = remoteJson["id"].Str();
            debugPrintf("Downloading remote.json from Google Drive with id %s\n",
                        remoteJsonId.c_str());
            downloadFile(remoteResourceFile, remoteJsonId);

            remoteResourceRoot = Json::Load(remoteResourceFile);

            downloadChanges();
            debugPrintf(
                "Data from Google Drive downloaded, Please restart app to upload changes\n");
        }
        else
        {
            debugPrintf("Wow, there's no remote.json on Google Drive!\n");
            // buildRemoteResourceTree();
            // downloadChanges();
        }
    }

    // TODO: After every scenario completion, write last sync date to config
    /*u64 epochTime;
    u64 nsec;

    sysGetCurrentTime(&epochTime, &nsec);

    last_sync = new std::string(epochTime);*/

    syncing = 0;

    msgDialogAbort();

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
                        debugPrintf("Quit Called...%d\n", exitapp.load());
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