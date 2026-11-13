// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "auth/AuthorizedQueue.h"
#include "net/HttpClient.h"
#include "sync/CloudStorage.h"
#include "json/Json.h"

#include <functional>
#include <string>
#include <vector>

class GoogleDeviceAuth;

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
 * Google Drive v2 for the signed-in user. Queued requests go out many at once on an
 * AuthorizedQueue; a queued file too big for one request goes up the resumable way once the
 * queue is through. Remembers when Drive refused an upload for lack of room.
 */
class GoogleDrive : public CloudStorage
{
  public:
    /**
     * @param http the connection for requests sent one by one
     * @param auth the sign-in whose token every request carries
     * @param parallel how many queued requests are sent at once at most
     * @param display what the app shows while a file moves by itself
     */
    GoogleDrive(HttpClient &http, GoogleDeviceAuth &auth, unsigned int parallel,
                const TransferDisplay &display);

    bool FindFolder(const std::string &title, const std::string &parentId, Json &folder) override;
    bool ListFiles(const std::string &folderId, Json::Array &files) override;
    Json CreateFolder(const std::string &title, const std::string &parentId) override;
    /**
     * Uploads up to 5 MB in one request, a bigger file (or one that holds the multipart boundary)
     * in a resumable session.
     */
    Json UploadFile(const std::string &path, const FileToStore &file,
                    const std::string &mimeType) override;
    bool DownloadFile(const std::string &fileId, const std::string &localPath) override;
    bool TrashFile(const std::string &fileId) override;

    /**
     * Sends the file as octetMime. A file that cannot go in one request waits until the queue is
     * through and then goes up the resumable way.
     */
    void QueueFile(const std::string &path, const PrepareFile &prepare, const Done &done) override;
    void RunQueue(const Stop &stop) override;
    /** Not counting a file that goes up the resumable way after the queue. */
    unsigned int Running() const override;

    bool IsFull() const override;
    Failure LastFailure() const override;

  private:
    GoogleDrive(const GoogleDrive &) = delete;
    GoogleDrive &operator=(const GoogleDrive &) = delete;

    /**
     * Builds the request that creates a folder.
     * @param title the folder's name
     * @param parentId the folder to create it in
     * @return the request, without the bearer token
     */
    QueuedRequest BuildFolderRequest(const std::string &title, const std::string &parentId);
    /**
     * Builds the request that creates or updates a file with its metadata and content in one
     * go, for the queue.
     * @param meta the file's metadata as JSON
     * @param mimeType the content's type
     * @param content the whole file
     * @param fileId the Drive file to update; empty to create a new one
     * @return the request, without the bearer token
     */
    QueuedRequest BuildMultipartRequest(const std::string &meta, const std::string &mimeType,
                                        const std::string &content, const std::string &fileId);
    /**
     * Logs a failed request, and remembers its status and whether Drive refused it for lack of
     * room.
     * @param what what failed, as the debug log names it
     * @param status the HTTP status, or below zero when no answer came
     * @param reply the body Drive answered with
     */
    void NoteFailure(const char *what, long status, const std::string &reply);
    /**
     * Hands Drive's answer to a queued request on, or notes why it failed.
     * @param what what failed, as the debug log names it
     * @param status the HTTP status, or below zero when no answer came
     * @param reply the body Drive answered with
     * @param done takes the item; without "id" when the request failed
     */
    void FinishQueued(const char *what, long status, const std::string &reply, const Done &done);

    /**
     * The first item a search finds.
     * @param query the search, in the Drive v2 query language
     * @param item receives the item; without "id" when nothing matches
     * @return false when the search failed, so whether the item exists is not known
     */
    bool FindFirst(const std::string &query, Json &item);

    /** Sends a file that fits in one request together with its metadata. */
    Json UploadInOneRequest(const std::string &path, const std::string &meta,
                            const std::string &mimeType, const std::string &content,
                            const std::string &fileId);
    /** Sends a file in a resumable session: the metadata first, then the content. */
    Json UploadResumable(const std::string &path, const std::string &meta,
                         const std::string &mimeType, long long size, const std::string &fileId);

    HttpClient &m_http;
    GoogleDeviceAuth &m_auth;
    TransferDisplay m_display;
    AuthorizedQueue m_queue;
    /** A queued file that cannot go in one request. */
    struct ResumableFile
    {
        std::string path;
        FileToStore file;
        Done done;
    };

    // The queued files that cannot go in one request, sent once the queue is through.
    std::vector<ResumableFile> m_resumable;
    bool m_full;
    long m_lastFailureStatus;
};
