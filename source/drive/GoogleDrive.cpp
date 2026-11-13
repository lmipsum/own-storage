// SPDX-License-Identifier: GPL-3.0-or-later
#include "drive/GoogleDrive.h"

#include "auth/AuthorizedSend.h"
#include "auth/GoogleDeviceAuth.h"
#include "app/DebugLog.h"
#include "app/Files.h"

#include <cstddef>
#include <sstream>

#include <sys/file.h>

namespace
{
const char kFilesUrl[] = "https://www.googleapis.com/drive/v2/files";
const char kUploadUrl[] = "https://www.googleapis.com/upload/drive/v2/files";
const char kFoundItemFields[] =
    "items(fileSize,id,md5Checksum,mimeType,modifiedDate,parents/id,title)";
// What Drive answers about a file it has just stored.
const char kStoredFileFields[] = "fileSize,id,md5Checksum,modifiedDate,parents/id";
const char kCreatedFolderFields[] = "id,modifiedDate,parents/id";
// Enough of an unexpected reply to recognise it in the debug log.
const std::size_t kMaxLoggedReplyLength = 200;

/** The first reason in a Drive error object, or "" when there is none. */
std::string ReadFirstErrorReason(const Json &error)
{
    return error["errors"][0u]["reason"].Str();
}

/** True when Drive refused a request because the user's Drive has no room left. */
bool IsDriveFullError(const std::string &reply)
{
    const std::string reason = ReadFirstErrorReason(Json::Parse(reply)["error"]);
    return reason == "quotaExceeded" || reason == "storageQuotaExceeded";
}
} // namespace

GoogleDrive::GoogleDrive(HttpClient &http, GoogleDeviceAuth &auth, const TransferDisplay &display)
    : m_http(http), m_auth(auth), m_display(display), m_full(false)
{
}

Json GoogleDrive::FindFirst(const std::string &query)
{
    const std::string url = std::string(kFilesUrl) + "?q=" + m_http.Escape(query) +
                            "&fields=" + m_http.Escape(kFoundItemFields);
    std::string reply;
    const long status =
        SendAuthorized(m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken)
                       { return m_http.Get(url, withToken, reply); });
    if (!HttpClient::Succeeded(status))
    {
        debugPrintf("FindFirst: HTTP Server Error...%ld %s\n", status,
                    DescribeDriveError(reply).c_str());
        return Json();
    }
    const Json::Array items = Json::Parse(reply)["items"].AsArray();
    return items.empty() ? Json() : items.front();
}

Json GoogleDrive::FindInFolder(const std::string &title, const std::string &mimeType,
                               const std::string &parentId)
{
    return FindFirst("mimeType='" + mimeType + "' and " + BuildTitleInFolderQuery(title, parentId));
}

Json GoogleDrive::CreateFolder(const std::string &title, const std::string &parentId)
{
    HttpClient::Headers headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("Expect:");
    const std::string url =
        std::string(kFilesUrl) + "?fields=" + m_http.Escape(kCreatedFolderFields);
    const std::string meta = BuildMetadata(title, folderMime, parentId);

    debugPrintf("Attempting folder creation on GDrive...\n");
    std::string reply;
    const long status = SendAuthorized(m_auth, headers, [&](const HttpClient::Headers &withToken)
                                       { return m_http.Post(url, meta, withToken, reply); });
    if (!HttpClient::Succeeded(status))
    {
        debugPrintf("CreateFolder: Could not create folder..%ld %s\n", status,
                    DescribeDriveError(reply).c_str());
        return Json();
    }
    const Json folder = Json::Parse(reply);
    if (folder.Has("id"))
    {
        debugPrintf("Folder id is %ld, %s\n", status, folder["id"].Str().c_str());
    }
    else
    {
        debugPrintf("Folder creation failed with status %ld\n", status);
    }
    return folder;
}

Json GoogleDrive::UploadFile(const std::string &path, const std::string &title,
                             const std::string &mimeType, const std::string &parentId,
                             const std::string &fileId)
{
    sysFSStat entry;
    if (sysFsStat(path.c_str(), &entry) != 0)
    {
        debugPrintf("UploadFile: Could not read %s\n", path.c_str());
        return Json();
    }
    const std::string meta = BuildMetadata(title, mimeType, parentId);
    std::string content;
    if (entry.st_size <= multipartUploadLimit && ReadWholeFile(path, content) &&
        content.find(multipartBoundary) == std::string::npos)
    {
        return UploadInOneRequest(path, meta, mimeType, content, fileId);
    }
    return UploadResumable(path, meta, mimeType, entry.st_size, fileId);
}

QueuedRequest GoogleDrive::BuildMultipartRequest(const std::string &meta,
                                                 const std::string &mimeType,
                                                 const std::string &content,
                                                 const std::string &fileId)
{
    QueuedRequest request;
    request.method = fileId.empty() ? "POST" : "PUT";
    request.url = std::string(kUploadUrl) + (fileId.empty() ? std::string() : "/" + fileId) +
                  "?uploadType=multipart&fields=" + m_http.Escape(kStoredFileFields);
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

Json GoogleDrive::UploadInOneRequest(const std::string &path, const std::string &meta,
                                     const std::string &mimeType, const std::string &content,
                                     const std::string &fileId)
{
    const QueuedRequest request = BuildMultipartRequest(meta, mimeType, content, fileId);
    m_display.begin(path);
    std::string reply;
    const long status = SendAuthorized(
        m_auth, request.headers,
        [&](const HttpClient::Headers &withToken)
        {
            return request.method == "PUT"
                       ? m_http.Put(request.url, request.body, withToken, reply)
                       : m_http.Post(request.url, request.body, withToken, reply);
        },
        request.retry);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("UploadInOneRequest: Could not upload file", status, reply);
        return Json();
    }
    m_display.end();

    const Json file = Json::Parse(reply);
    debugPrintf("Resource id is %s\n", file["id"].Str().c_str());
    return file;
}

Json GoogleDrive::UploadResumable(const std::string &path, const std::string &meta,
                                  const std::string &mimeType, long long size,
                                  const std::string &fileId)
{
    std::ostringstream announcedSize;
    announcedSize << "X-Upload-Content-Length: " << size;
    HttpClient::Headers sessionHeaders;
    sessionHeaders.push_back("Content-Type: application/json");
    sessionHeaders.push_back("X-Upload-Content-Type: " + mimeType);
    sessionHeaders.push_back(announcedSize.str());
    sessionHeaders.push_back("Expect:");
    const std::string sessionUrl =
        std::string(kUploadUrl) + (fileId.empty() ? std::string() : "/" + fileId) +
        "?uploadType=resumable&fields=" + m_http.Escape(kStoredFileFields);

    std::string sessionReply;
    const long sessionStatus =
        SendAuthorized(m_auth, sessionHeaders,
                       [&](const HttpClient::Headers &withToken)
                       {
                           return fileId.empty()
                                      ? m_http.Post(sessionUrl, meta, withToken, sessionReply)
                                      : m_http.Put(sessionUrl, meta, withToken, sessionReply);
                       });
    if (!HttpClient::Succeeded(sessionStatus))
    {
        NoteFailure("UploadResumable: Could not pose meta data", sessionStatus, sessionReply);
        return Json();
    }

    std::ostringstream contentSize;
    contentSize << "Content-Length: " << size;
    HttpClient::Headers contentHeaders;
    contentHeaders.push_back("Content-Type: " + mimeType);
    contentHeaders.push_back(contentSize.str());
    contentHeaders.push_back("Expect:");
    contentHeaders.push_back("Accept:");
    contentHeaders.push_back("Accept-Encoding: gzip");
    // Drive names the address of the session's content in the Location header.
    const std::string contentUrl = m_http.Location();

    m_display.begin(path);
    std::string reply;
    const long status = SendAuthorized(
        m_auth, contentHeaders, [&](const HttpClient::Headers &withToken)
        { return m_http.Upload(contentUrl, withToken, path, m_display.progress, reply); });
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("UploadResumable: Could not upload file", status, reply);
        return Json();
    }

    const Json file = Json::Parse(reply);
    if (file.Has("id"))
    {
        debugPrintf("Resource id is %s\n", file["id"].Str().c_str());
    }
    else
    {
        debugPrintf("UploadResumable: Resource id not found\n");
    }
    return file;
}

bool GoogleDrive::DownloadFile(const std::string &fileId, const std::string &localPath)
{
    const std::string url = std::string(kFilesUrl) + "/" + fileId + "?fields=downloadUrl";
    std::string reply;
    long status =
        SendAuthorized(m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken)
                       { return m_http.Get(url, withToken, reply); });
    if (!HttpClient::Succeeded(status))
    {
        debugPrintf("DownloadFile: Could not get downloadUrl...%ld %s\n", status,
                    DescribeDriveError(reply).c_str());
        return false;
    }
    const Json file = Json::Parse(reply);
    if (!file.Has("downloadUrl"))
    {
        return false;
    }
    const std::string downloadUrl = file["downloadUrl"].Str();
    debugPrintf("Downlink is %s\n", downloadUrl.c_str());

    m_display.begin(localPath);
    status = SendAuthorized(
        m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken)
        { return m_http.Download(downloadUrl, withToken, localPath, m_display.progress); });
    debugPrintf("Return Code for file download is %ld\n", status);
    if (!HttpClient::Succeeded(status))
    {
        debugPrintf("DownloadFile: Could not download File..%ld\n", status);
        return false;
    }
    return true;
}

void GoogleDrive::NoteFailure(const char *what, long status, const std::string &reply)
{
    debugPrintf("%s..%ld %s\n", what, status, DescribeDriveError(reply).c_str());
    if (IsDriveFullError(reply))
    {
        m_full = true;
    }
}

bool GoogleDrive::IsFull() const
{
    return m_full;
}

std::string BuildTitleInFolderQuery(const std::string &title, const std::string &parentId)
{
    return "trashed=false and title='" + title + "' and '" + parentId + "' in parents";
}

std::string BuildMetadata(const std::string &title, const std::string &mimeType,
                          const std::string &parentId)
{
    Json parent;
    parent.Add("id", Json(parentId));
    Json::Array parents;
    parents.push_back(parent);

    Json meta;
    meta.Add("title", Json(title));
    meta.Add("parents", Json(parents));
    meta.Add("mimeType", Json(mimeType));
    return meta.Str();
}

std::string DescribeDriveError(const std::string &reply)
{
    const Json error = Json::Parse(reply)["error"];
    if (error.IsEmpty())
    {
        return reply.substr(0, kMaxLoggedReplyLength);
    }
    return ReadFirstErrorReason(error) + ": " + error["message"].Str();
}
