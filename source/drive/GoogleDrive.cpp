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
// What a listing gives of each file, and how many files one request may give.
const char kListedFields[] = "kind,items(description,id,md5Checksum,title),nextPageToken";
const char kTrashedFileFields[] = "id";
// What a listing reply says it is; any other reply is not a listing, however it ends.
const char kFileListKind[] = "drive#fileList";
const char kListPageSize[] = "1000";
// Enough of an unexpected reply to recognise it in the debug log.
const std::size_t kMaxLoggedReplyLength = 200;
// Drive takes metadata and content in one request up to 5 MB; bigger files go up in a
// resumable session.
const long kMultipartUploadLimit = 5 * 1024 * 1024;
// Separates metadata and content in a multipart upload; a file that holds it goes up resumable.
const std::string kMultipartBoundary = "own_storage_part_7d1c4f0b9a2e";

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

/**
 * Builds the metadata Drive needs to create or update an item.
 * @param file the item's name, description and folder
 * @param mimeType its type
 * @return the metadata as JSON text
 */
std::string BuildMetadata(const FileToStore &file, const std::string &mimeType)
{
    Json parent;
    parent.Add("id", Json(file.parentId));
    Json::Array parents;
    parents.push_back(parent);

    Json meta;
    meta.Add("title", Json(file.title));
    if (!file.description.empty())
    {
        meta.Add("description", Json(file.description));
    }
    meta.Add("parents", Json(parents));
    meta.Add("mimeType", Json(mimeType));
    return meta.Str();
}

/**
 * Describes a failed Drive request for the debug log, from its JSON error reply.
 * @param reply the body Drive answered with
 * @return "reason: message", or the start of the body when it is not a Drive error
 */
std::string DescribeDriveError(const std::string &reply)
{
    const Json error = Json::Parse(reply)["error"];
    if (error.IsEmpty())
    {
        return reply.substr(0, kMaxLoggedReplyLength);
    }
    return ReadErrorReason(reply) + ": " + error["message"].Str();
}
} // namespace

GoogleDrive::GoogleDrive(HttpClient &http, GoogleDeviceAuth &auth, unsigned int parallel,
                         const TransferDisplay &display)
    : m_http(http), m_auth(auth), m_display(display), m_queue(auth, parallel), m_full(false),
      m_lastFailureStatus(0)
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

bool GoogleDrive::FindFolder(const std::string &title, const std::string &parentId, Json &folder)
{
    return FindFirst("mimeType=" + QuoteQueryValue(folderMime) +
                         " and trashed=false and title=" + QuoteQueryValue(title) + " and " +
                         QuoteQueryValue(parentId) + " in parents",
                     folder);
}

bool GoogleDrive::ListFiles(const std::string &folderId, Json::Array &files)
{
    files.clear();
    const std::string query = QuoteQueryValue(folderId) + " in parents and trashed=false and " +
                              "mimeType!=" + QuoteQueryValue(folderMime);
    const std::string firstPage = std::string(kFilesUrl) + "?maxResults=" + kListPageSize +
                                  "&q=" + m_http.Escape(query) +
                                  "&fields=" + m_http.Escape(kListedFields);
    std::string pageToken;
    do
    {
        const std::string url =
            pageToken.empty() ? firstPage : firstPage + "&pageToken=" + m_http.Escape(pageToken);
        std::string reply;
        const long status = SendAuthorized(
            m_auth, HttpClient::Headers(),
            [&](const HttpClient::Headers &withToken, std::string &body)
            { return m_http.Get(url, withToken, body); }, reply);
        if (!HttpClient::Succeeded(status))
        {
            NoteFailure("ListFiles: Could not list", status, reply);
            return false;
        }
        const Json page = Json::Parse(reply);
        if (page["kind"].Str() != kFileListKind)
        {
            NoteFailure("ListFiles: The answer is not a listing", status, reply);
            return false;
        }
        for (const Json &file : page["items"].AsArray())
        {
            files.push_back(file);
        }
        pageToken = page["nextPageToken"].Str();
    } while (!pageToken.empty());
    return true;
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
    request.body =
        BuildMetadata(FileToStore{title, std::string(), parentId, std::string()}, folderMime);
    // A create sent twice makes two folders, so a lost answer is left to the next run, which
    // looks up the Own Storage folder at every start.
    request.retry = Retry::OnlyRefused;
    return request;
}

Json GoogleDrive::UploadFile(const std::string &path, const FileToStore &file,
                             const std::string &mimeType)
{
    sysFSStat entry;
    if (sysFsStat(path.c_str(), &entry) != 0)
    {
        debugPrintf("UploadFile: Could not read %s\n", path.c_str());
        return Json();
    }
    const std::string meta = BuildMetadata(file, mimeType);
    std::string content;
    if (entry.st_size <= kMultipartUploadLimit && ReadWholeFile(path, content) &&
        content.find(kMultipartBoundary) == std::string::npos)
    {
        return UploadInOneRequest(path, meta, mimeType, content, file.fileId);
    }
    return UploadResumable(path, meta, mimeType, entry.st_size, file.fileId);
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
    request.headers.push_back("Content-Type: multipart/related; boundary=" + kMultipartBoundary);
    request.headers.push_back("Expect:");
    request.body = "--" + kMultipartBoundary +
                   "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta + "\r\n--" +
                   kMultipartBoundary + "\r\nContent-Type: " + mimeType + "\r\n\r\n" + content +
                   "\r\n--" + kMultipartBoundary + "--\r\n";
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

bool GoogleDrive::TrashFile(const std::string &fileId)
{
    const std::string url = std::string(kFilesUrl) + "/" + fileId +
                            "/trash?fields=" + m_http.Escape(kTrashedFileFields);
    std::string reply;
    // Trashing twice leaves the file in the trash all the same, so it is safe to send again.
    const long status = SendAuthorized(
        m_auth, HttpClient::Headers(1, "Expect:"),
        [&](const HttpClient::Headers &withToken, std::string &body)
        { return m_http.Post(url, std::string(), withToken, body); }, reply);
    if (!HttpClient::Succeeded(status))
    {
        NoteFailure("TrashFile: Could not trash file", status, reply);
        return false;
    }
    debugPrintf("TrashFile: trashed %s\n", fileId.c_str());
    return true;
}

void GoogleDrive::QueueFile(const std::string &path, const PrepareFile &prepare, const Done &done)
{
    m_queue.Add(
        [this, path, prepare, done](QueuedRequest &request)
        {
            FileToStore file;
            if (!prepare(file))
            {
                return false;
            }
            sysFSStat entry;
            std::string content;
            if (sysFsStat(path.c_str(), &entry) != 0 || entry.st_size > kMultipartUploadLimit ||
                !ReadWholeFile(path, content) ||
                content.find(kMultipartBoundary) != std::string::npos)
            {
                m_resumable.push_back(ResumableFile{path, file, done});
                return false;
            }
            request = BuildMultipartRequest(BuildMetadata(file, octetMime), octetMime, content,
                                            file.fileId);
            return true;
        },
        [this, done](long status, const std::string &reply)
        { FinishQueued("QueueFile: Could not upload file", status, reply, done); });
}

void GoogleDrive::RunQueue(const Stop &stop)
{
    m_queue.Run(stop);
    // A callback may queue more, so the loop goes over a list of its own.
    std::vector<ResumableFile> resumable;
    resumable.swap(m_resumable);
    for (const ResumableFile &queued : resumable)
    {
        if (stop())
        {
            break;
        }
        queued.done(UploadFile(queued.path, queued.file, octetMime));
    }
}

unsigned int GoogleDrive::Running() const
{
    return m_queue.Running();
}

void GoogleDrive::FinishQueued(const char *what, long status, const std::string &reply,
                               const Done &done)
{
    const Json item = Json::Parse(reply);
    if (!HttpClient::Succeeded(status) || !item.Has("id"))
    {
        NoteFailure(what, status, reply);
        done(Json());
        return;
    }
    done(item);
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

Failure GoogleDrive::LastFailure() const
{
    if (m_lastFailureStatus == HttpClient::kLocalError)
    {
        return Failure::LocalFile;
    }
    return m_lastFailureStatus < 0 ? Failure::Unreachable : Failure::Refused;
}
