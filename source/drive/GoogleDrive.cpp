// SPDX-License-Identifier: GPL-3.0-or-later
#include "drive/GoogleDrive.h"

#include "auth/AuthorizedSend.h"
#include "auth/GoogleDeviceAuth.h"
#include "net/HttpClient.h"
#include "app/DebugLog.h"

#include <fstream>
#include <sstream>

#include <sys/file.h>
#include <sysutil/msg.h>

// TODO: main.cpp still owns these; GoogleDrive takes them over as members (#23).
extern HttpClient http;
extern GoogleDeviceAuth googleAuth;
extern double progFuncLastProgress;
int transferProgress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);
void recordUploadFailure(const char *what, long status, const std::string &reply);

const std::string queryFields =
    "items(fileSize,id,md5Checksum,mimeType,modifiedDate,parents/id,title)";
// What Drive answers about a file it has just stored.
const std::string uploadedFileFields = "fileSize,id,md5Checksum,modifiedDate,parents/id";
// Enough of an unexpected reply to recognise it in the debug log.
const std::size_t maxLoggedReplyLength = 200;

/**
 * The first reason in a Drive error object.
 * @param error the "error" member of a Drive reply
 * @return the reason, or "" when there is none
 */
std::string readFirstErrorReason(const Json &error)
{
    return error["errors"][0u]["reason"].Str();
}

/**
 * Describes a failed Drive request for the debug log, from its JSON error reply.
 * @param reply the body Drive answered with
 * @return "reason: message", or the start of the body when it is not a Drive error
 */
std::string describeDriveError(const std::string &reply)
{
    const Json error = Json::Parse(reply)["error"];
    if (error.IsEmpty())
    {
        return reply.substr(0, maxLoggedReplyLength);
    }
    return readFirstErrorReason(error) + ": " + error["message"].Str();
}

/**
 * Whether Drive refused a request because the user's Drive has no room left.
 * @param reply the body Drive answered with
 * @return true for the quotaExceeded and storageQuotaExceeded reasons
 */
bool isDriveFullError(const std::string &reply)
{
    const std::string reason = readFirstErrorReason(Json::Parse(reply)["error"]);
    return reason == "quotaExceeded" || reason == "storageQuotaExceeded";
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
        debugPrintf("getResourcesUnderFolder: HTTP Server Error...%ld %s\n", ret,
                    describeDriveError(resp).c_str());
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
 * The first Drive item that a search finds.
 * @param query the search, in the Drive v2 query language
 * @return the item; without "id" when nothing matches or the request fails
 */
Json findFirstRemoteItem(const std::string &query)
{
    long ret;
    Json resourceObject;

    HttpClient::Headers hdr;

    std::string resp;

    std::string url = "https://www.googleapis.com/drive/v2/files?q=" + http.Escape(query) +
                      "&fields=" + http.Escape(queryFields);

    ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                         { return http.Get(url, headers, resp); });

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("findFirstRemoteItem: HTTP Server Error...%ld %s\n", ret,
                    describeDriveError(resp).c_str());
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
 * Builds the Drive search for an item that is not trashed, by its title and folder.
 * @param title the item's title
 * @param parentId the Drive folder that holds the item
 * @return the query, in the Drive v2 query language
 */
std::string buildTitleInFolderQuery(const std::string &title, const std::string &parentId)
{
    return "trashed=false and title='" + title + "' and '" + parentId + "' in parents";
}

/**
 * The function checks if a resource exists on google drive and returns the item
 * @param resourceTitle the title of the resource to find
 * @param mimeType the mimetype of the resource
 * @param parentId the parentid of the resource's parent (if you don't know this, this function is
 * useless, unless you're searching under root)
 * @return the item; without "id" when there is none
 */
Json checkIfRemoteResourceExists(std::string resourceTitle, std::string mimeType,
                                 std::string parentId)
{
    return findFirstRemoteItem("mimeType='" + mimeType + "' and " +
                               buildTitleInFolderQuery(resourceTitle, parentId));
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
        debugPrintf("downloadFile: Could not get downloadUrl...%ld %s\n", ret,
                    describeDriveError(resp).c_str());
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

    debugPrintf("Return Code for file download is %ld\n", ret);

    if (!HttpClient::Succeeded(ret))
    {
        debugPrintf("downloadFile: Could not download File..%ld\n", ret);
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return false;
    }

    return true;
}

/**
 * Reads a whole file.
 * @param path the file
 * @param text receives the content
 * @return false when the file cannot be read
 */
bool readWholeFile(const std::string &path, std::string &text)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream content;
    content << file.rdbuf();
    text = content.str();
    return true;
}

/**
 * Builds the request that creates or updates a file on Drive with its metadata and content
 * in one go.
 * @param meta the file's metadata as JSON
 * @param mimeType the content's type
 * @param content the whole file
 * @param fileId the Drive file to update; empty to create a new one
 * @return the request, without the bearer token
 */
QueuedRequest buildMultipartRequest(const std::string &meta, const std::string &mimeType,
                                    const std::string &content, const std::string &fileId)
{
    QueuedRequest request;
    request.method = fileId.empty() ? "POST" : "PUT";
    request.url = "https://www.googleapis.com/upload/drive/v2/files" +
                  (fileId.empty() ? std::string() : "/" + fileId) +
                  "?uploadType=multipart&fields=" + http.Escape(uploadedFileFields);
    request.headers.push_back("Content-Type: multipart/related; boundary=" + multipartBoundary);
    request.headers.push_back("Expect:");
    request.body = "--" + multipartBoundary +
                   "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta + "\r\n--" +
                   multipartBoundary + "\r\nContent-Type: " + mimeType + "\r\n\r\n" + content +
                   "\r\n--" + multipartBoundary + "--\r\n";
    // A create sent twice makes two files, so it is not sent again when its answer is lost;
    // the entry stays in creatingStatus and the next run looks for the file instead.
    request.retry = fileId.empty() ? Retry::OnlyAnswered : Retry::AlsoUnanswered;
    return request;
}

/**
 * Creates or updates a file on Drive with its metadata and content in one request.
 * @param meta the file's metadata as JSON
 * @param mimeType the content's type
 * @param content the whole file
 * @param fileId the Drive file to update; empty to create a new one
 * @param path the local file, for the progress bar
 * @return the stored file; without "id" when the request failed
 */
Json uploadMultipart(const std::string &meta, const std::string &mimeType,
                     const std::string &content, const std::string &fileId, const std::string &path)
{
    const QueuedRequest request = buildMultipartRequest(meta, mimeType, content, fileId);

    msgDialogProgressBarReset(MSG_PROGRESSBAR_INDEX1);
    msgDialogProgressBarSetMsg(MSG_PROGRESSBAR_INDEX1, path.c_str());

    std::string reply;
    const long ret = SendAuthorized(
        googleAuth, request.headers,
        [&](const HttpClient::Headers &withToken)
        {
            return request.method == "PUT" ? http.Put(request.url, request.body, withToken, reply)
                                           : http.Post(request.url, request.body, withToken, reply);
        },
        request.retry);
    if (!HttpClient::Succeeded(ret))
    {
        recordUploadFailure("uploadMultipart: Could not upload file", ret, reply);
        return Json();
    }
    msgDialogProgressBarInc(MSG_PROGRESSBAR_INDEX1, 100);

    const Json obj = Json::Parse(reply);
    debugPrintf("Resource id is %s\n", obj["id"].Str().c_str());
    return obj;
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
                std::string fileId)
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

    std::string content;
    if (entry.st_size <= multipartUploadLimit && readWholeFile(path, content) &&
        content.find(multipartBoundary) == std::string::npos)
    {
        return uploadMultipart(meta, mimeType, content, fileId, path);
    }

    std::string resp;

    std::string postUrl;

    if (fileId == "")
    {
        postUrl = "https://www.googleapis.com/upload/drive/v2/files?uploadType=resumable&fields=" +
                  http.Escape(uploadedFileFields);
        ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                             { return http.Post(postUrl, meta, headers, resp); });
    }
    else
    {
        postUrl = "https://www.googleapis.com/upload/drive/v2/files/" + fileId +
                  "?uploadType=resumable&fields=" + http.Escape(uploadedFileFields);
        ret = SendAuthorized(googleAuth, hdr, [&](const HttpClient::Headers &headers)
                             { return http.Put(postUrl, meta, headers, resp); });
    }

    if (!HttpClient::Succeeded(ret))
    {
        recordUploadFailure("uploadFile: Could not pose meta data", ret, resp);
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
        recordUploadFailure("uploadFile: Could not upload file", ret, resp2);
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
        debugPrintf("uploadDirectory: Could not upload directory..%ld %s\n", ret,
                    describeDriveError(resp).c_str());
        // This could mean the internet disconnected, or the server crashed or whatever
        // Possibly exit the application?
        return obj;
    }

    obj = Json::Parse(resp);
    if (obj.Has("id"))
    {
        debugPrintf("Folder id is %ld, %s\n", ret, obj["id"].Str().c_str());
    }
    else
    {
        debugPrintf("Folder creation failed with status %ld\n", ret);
    }

    return obj;
}
