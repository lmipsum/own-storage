// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "auth/AuthorizedQueue.h"
#include "net/HttpClient.h"
#include "json/Json.h"

#include <functional>
#include <string>

class GoogleDeviceAuth;

const std::string jsonMime = "application/json";
const std::string folderMime = "application/vnd.google-apps.folder";
const std::string octetMime = "application/octet-stream";
// Drive takes metadata and content in one request up to 5 MB; bigger files go up in a
// resumable session.
const long multipartUploadLimit = 5 * 1024 * 1024;
// Separates metadata and content in a multipart upload; a file that holds it goes up resumable.
const std::string multipartBoundary = "own_storage_part_7d1c4f0b9a2e";

/** What the app shows while GoogleDrive moves a file by itself. */
struct TransferDisplay
{
    /** A file starts to move; the argument is the local file. */
    std::function<void(const std::string &)> begin;
    /** curl's callback with the bytes moved so far. */
    HttpClient::Progress progress;
    /** A file moved in one go, without reports on the way. */
    std::function<void()> end;
};

/**
 * Google Drive v2 for the signed-in user: searches, folders, and files sent and fetched one at
 * a time. Remembers when Drive refused an upload for lack of room.
 */
class GoogleDrive
{
  public:
    GoogleDrive(HttpClient &http, GoogleDeviceAuth &auth, const TransferDisplay &display);

    /**
     * The first item a search finds.
     * @param query the search, in the Drive v2 query language
     * @return the item; without "id" when nothing matches or the request fails
     */
    Json FindFirst(const std::string &query);
    /**
     * The item of a title and type in a folder, unless it is trashed.
     * @param title the item's title
     * @param mimeType the item's type
     * @param parentId the folder that holds it
     * @return the item; without "id" when there is none
     */
    Json FindInFolder(const std::string &title, const std::string &mimeType,
                      const std::string &parentId);
    /**
     * Creates a folder.
     * @param title the folder's name
     * @param parentId the folder to create it in; "root" for the top of Drive
     * @return the folder; without "id" when the request failed
     */
    Json CreateFolder(const std::string &title, const std::string &parentId);
    /**
     * Uploads a local file: up to multipartUploadLimit bytes in one request, a bigger one (or one
     * that holds the multipart boundary) in a resumable session.
     * @param path the local file
     * @param title its name on Drive
     * @param mimeType its type
     * @param parentId the folder that holds it
     * @param fileId the Drive file to update; empty to create a new one
     * @return the stored file; without "id" when the upload failed
     */
    Json UploadFile(const std::string &path, const std::string &title, const std::string &mimeType,
                    const std::string &parentId, const std::string &fileId = std::string());
    /**
     * Saves a Drive file to a local path.
     * @param fileId the Drive file
     * @param localPath where to save it
     * @return false when the download failed
     */
    bool DownloadFile(const std::string &fileId, const std::string &localPath);
    /**
     * Builds the request that creates or updates a file with its metadata and content in one
     * go, for AuthorizedQueue.
     * @param meta the file's metadata as JSON
     * @param mimeType the content's type
     * @param content the whole file
     * @param fileId the Drive file to update; empty to create a new one
     * @return the request, without the bearer token
     */
    QueuedRequest BuildMultipartRequest(const std::string &meta, const std::string &mimeType,
                                        const std::string &content, const std::string &fileId);
    /**
     * Logs a failed request, and remembers when Drive refused it for lack of room.
     * @param what what failed, as the debug log names it
     * @param status the HTTP status, or -1 when no answer came
     * @param reply the body Drive answered with
     */
    void NoteFailure(const char *what, long status, const std::string &reply);
    /** Whether Drive has refused an upload for lack of room. */
    bool IsFull() const;

  private:
    GoogleDrive(const GoogleDrive &) = delete;
    GoogleDrive &operator=(const GoogleDrive &) = delete;

    /** Sends a file of up to multipartUploadLimit bytes with its metadata in one request. */
    Json UploadInOneRequest(const std::string &path, const std::string &meta,
                            const std::string &mimeType, const std::string &content,
                            const std::string &fileId);
    /** Sends a file in a resumable session: the metadata first, then the content. */
    Json UploadResumable(const std::string &path, const std::string &meta,
                         const std::string &mimeType, long long size, const std::string &fileId);

    HttpClient &m_http;
    GoogleDeviceAuth &m_auth;
    TransferDisplay m_display;
    bool m_full;
};

/**
 * Builds the Drive search for an item that is not trashed, by its title and folder.
 * @param title the item's title
 * @param parentId the Drive folder that holds the item
 * @return the query, in the Drive v2 query language
 */
std::string BuildTitleInFolderQuery(const std::string &title, const std::string &parentId);

/**
 * Builds the metadata Drive needs to create an item.
 * @param title the item's name
 * @param mimeType its type
 * @param parentId the folder that holds it
 * @return the metadata as JSON text
 */
std::string BuildMetadata(const std::string &title, const std::string &mimeType,
                          const std::string &parentId);

/**
 * Describes a failed Drive request for the debug log, from its JSON error reply.
 * @param reply the body Drive answered with
 * @return "reason: message", or the start of the body when it is not a Drive error
 */
std::string DescribeDriveError(const std::string &reply);
