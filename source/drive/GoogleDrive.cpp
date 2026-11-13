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

/**
 * Quotes a value for the Drive query language, where ' ends a string and \ escapes.
 * @param value the text to search for
 * @return the value between single quotes, with \ and ' escaped
 */
std::string QuoteQueryValue(const std::string &value)
{
    std::string quoted = "'";
    for (const char c : value)
    {
        if (c == '\\' || c == '\'')
        {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "'";
}

/** True when Drive refused a request because the user's Drive has no room left. */
bool IsDriveFullError(const std::string &reply)
{
    const std::string reason = ReadErrorReason(reply);
    return reason == "quotaExceeded" || reason == "storageQuotaExceeded";
}
} // namespace

GoogleDrive::GoogleDrive(HttpClient &http, GoogleDeviceAuth &auth, const TransferDisplay &display)
    : m_http(http), m_auth(auth), m_display(display), m_full(false), m_lastFailureStatus(0)
{
}

bool GoogleDrive::FindFirst(const std::string &query, Json &item)
{
    item = Json();
    const std::string url = std::string(kFilesUrl) + "?q=" + m_http.Escape(query) +
                            "&fields=" + m_http.Escape(kFoundItemFields);
    std::string reply;
    const long status = SendAuthorized(
        m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken, std::string &body)
        { return m_http.Get(url, withToken, body); }, reply);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("FindFirst: Could not search", status, reply);
        return false;
    }
    const Json::Array items = Json::Parse(reply)["items"].AsArray();
    if (!items.empty())
    {
        item = items.front();
    }
    return true;
}

bool GoogleDrive::FindInFolder(const std::string &title, ItemKind kind, const std::string &parentId,
                               Json &item)
{
    // Drive may store an uploaded file under another type than the one sent, so a file is found
    // by not being a folder.
    const std::string typeCondition =
        std::string(kind == ItemKind::Folder ? "mimeType=" : "mimeType!=") +
        QuoteQueryValue(folderMime);
    return FindFirst(typeCondition + " and trashed=false and title=" + QuoteQueryValue(title) +
                         " and " + QuoteQueryValue(parentId) + " in parents",
                     item);
}

Json GoogleDrive::CreateFolder(const std::string &title, const std::string &parentId)
{
    const QueuedRequest request = BuildFolderRequest(title, parentId);
    std::string reply;
    const long status = SendAuthorized(
        m_auth, request.headers, [&](const HttpClient::Headers &withToken, std::string &body)
        { return m_http.Post(request.url, request.body, withToken, body); }, reply, request.retry);
    const Json folder = Json::Parse(reply);
    if (!HttpClient::Succeeded(status) || !folder.Has("id"))
    {
        NoteFailure("CreateFolder: Could not create folder", status, reply);
        return Json();
    }
    debugPrintf("CreateFolder: created %s\n", folder["id"].Str().c_str());
    return folder;
}

QueuedRequest GoogleDrive::BuildFolderRequest(const std::string &title, const std::string &parentId)
{
    QueuedRequest request;
    request.method = "POST";
    request.url = std::string(kFilesUrl) + "?fields=" + m_http.Escape(kCreatedFolderFields);
    request.headers.push_back("Content-Type: " + jsonMime);
    request.headers.push_back("Expect:");
    request.body = BuildMetadata(title, folderMime, parentId);
    // A create sent twice makes two folders, so a lost answer is left to the next run, which
    // looks up a folder in creatingStatus, and the Own Storage folder at every start.
    request.retry = Retry::OnlyRefused;
    return request;
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
    // A create sent twice makes two files, so it is not sent again when it may have been done;
    // the caller looks for the file on the next run instead.
    request.retry = fileId.empty() ? Retry::OnlyRefused : Retry::AnyTransient;
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
        [&](const HttpClient::Headers &withToken, std::string &body)
        {
            return request.method == "PUT"
                       ? m_http.Put(request.url, request.body, withToken, body)
                       : m_http.Post(request.url, request.body, withToken, body);
        },
        reply, request.retry);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("UploadInOneRequest: Could not upload file", status, reply);
        return Json();
    }
    m_display.end();

    const Json file = Json::Parse(reply);
    debugPrintf("UploadInOneRequest: stored %s\n", file["id"].Str().c_str());
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
    const long sessionStatus = SendAuthorized(
        m_auth, sessionHeaders,
        [&](const HttpClient::Headers &withToken, std::string &body)
        {
            return fileId.empty() ? m_http.Post(sessionUrl, meta, withToken, body)
                                  : m_http.Put(sessionUrl, meta, withToken, body);
        },
        sessionReply);
    if (!HttpClient::Succeeded(sessionStatus))
    {
        NoteFailure("UploadResumable: Could not start the upload session", sessionStatus,
                    sessionReply);
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
        m_auth, contentHeaders, [&](const HttpClient::Headers &withToken, std::string &body)
        { return m_http.Upload(contentUrl, withToken, path, m_display.progress, body); }, reply);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("UploadResumable: Could not upload file", status, reply);
        return Json();
    }

    const Json file = Json::Parse(reply);
    if (file.Has("id"))
    {
        debugPrintf("UploadResumable: stored %s\n", file["id"].Str().c_str());
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
    long status = SendAuthorized(
        m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken, std::string &body)
        { return m_http.Get(url, withToken, body); }, reply);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("DownloadFile: Could not get downloadUrl", status, reply);
        return false;
    }
    const Json file = Json::Parse(reply);
    if (!file.Has("downloadUrl"))
    {
        NoteFailure("DownloadFile: No downloadUrl", status, reply);
        return false;
    }
    const std::string downloadUrl = file["downloadUrl"].Str();

    m_display.begin(localPath);
    // The content goes to the file, so this reply stays empty.
    std::string unusedReply;
    status = SendAuthorized(
        m_auth, HttpClient::Headers(), [&](const HttpClient::Headers &withToken, std::string &)
        { return m_http.Download(downloadUrl, withToken, localPath, m_display.progress); },
        unusedReply);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("DownloadFile: Could not download file", status, unusedReply);
        return false;
    }
    return true;
}

void GoogleDrive::NoteFailure(const char *what, long status, const std::string &reply)
{
    debugPrintf("%s..%ld %s\n", what, status, DescribeDriveError(reply).c_str());
    m_lastFailureStatus = status;
    if (IsDriveFullError(reply))
    {
        m_full = true;
    }
}

bool GoogleDrive::IsFull() const
{
    return m_full;
}

long GoogleDrive::LastFailureStatus() const
{
    return m_lastFailureStatus;
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
    return ReadErrorReason(reply) + ": " + error["message"].Str();
}
