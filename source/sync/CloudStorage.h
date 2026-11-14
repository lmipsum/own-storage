// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"

#include <functional>
#include <string>

/** The type Drive gives a folder. */
const std::string folderMime = "application/vnd.google-apps.folder";
/** The type a save file is sent with. */
const std::string octetMime = "application/octet-stream";
/** The type of the metadata that creates a folder. */
const std::string jsonMime = "application/json";
/**
 * The property of a stored file that marks it as kept in the cloud only: no sync brings it down
 * by itself. Public, so a sign-in with a Google client of the user's own sees it too.
 */
const std::string cloudOnlyProperty = "ownStorageCloudOnly";
/** What cloudOnlyProperty holds on a file with the mark, and on one without. */
const std::string cloudOnlyMarked = "yes";
const std::string cloudOnlyUnmarked = "no";

/** The likely cause of the last failed request. */
enum class Failure
{
    /** No answer came, which points to the network. */
    Unreachable,
    /** The cloud answered with an error. */
    Refused,
    /** A local file could not be read or written. */
    LocalFile
};

/** A local file to store, as decided when its turn in the queue comes. */
struct FileToStore
{
    std::string title;
    /** What the cloud shows about it; may be empty. */
    std::string description;
    /** The folder that holds it. */
    std::string parentId;
    /** The stored file to update; empty to create a new one. */
    std::string fileId;
    /** Whether it is marked as kept in the cloud only; an upload without the mark clears it. */
    bool cloudOnly;
};

/**
 * Where the backup is kept, as the sync needs it: searches, listings, folders and files one by
 * one, and a queue that sends many files at once. Google Drive is one.
 */
class CloudStorage
{
  public:
    /** Fills in a queued file when its turn comes; false when nothing is to be sent. */
    typedef std::function<bool(FileToStore &file)> PrepareFile;
    /** Takes what the cloud says about the item; without "id" when the request failed. */
    typedef std::function<void(const Json &item)> Done;
    /** Asked between requests; true lets the running ones finish and drops the rest. */
    typedef std::function<bool()> Stop;

    virtual ~CloudStorage() = default;

    /**
     * The folder of a title in a folder, unless it is trashed.
     * @param title the folder's title
     * @param parentId the folder that holds it
     * @param folder receives the folder; without "id" when there is none
     * @return false when the search failed, so whether the folder exists is not known
     */
    virtual bool FindFolder(const std::string &title, const std::string &parentId,
                            Json &folder) = 0;
    /**
     * Every file in a folder, unless it is trashed; folders in it are left out.
     * @param folderId the folder
     * @param files receives what the cloud says about each file, with its "id", "title",
     * "description", "md5Checksum", "fileSize", "modifiedDate" and "properties"
     * @return false when the listing failed
     */
    virtual bool ListFiles(const std::string &folderId, Json::Array &files) = 0;
    /**
     * Creates a folder.
     * @param title the folder's name
     * @param parentId the folder to create it in; "root" for the top of the storage
     * @return the folder; without "id" when the request failed
     */
    virtual Json CreateFolder(const std::string &title, const std::string &parentId) = 0;
    /**
     * Stores a local file.
     * @param path the local file
     * @param file where and how to store it
     * @param mimeType its type
     * @return the stored file; without "id" when the upload failed
     */
    virtual Json UploadFile(const std::string &path, const FileToStore &file,
                            const std::string &mimeType) = 0;
    /**
     * Saves a stored file to a local path.
     * @param fileId the stored file
     * @param localPath where to save it
     * @return false when the download failed
     */
    virtual bool DownloadFile(const std::string &fileId, const std::string &localPath) = 0;
    /**
     * Moves a stored file to the trash, where the user can still restore it for a while.
     * @param fileId the stored file
     * @return false when the request failed
     */
    virtual bool TrashFile(const std::string &fileId) = 0;

    /**
     * Queues storing a file; nothing is sent before RunQueue().
     * @param path the local file
     * @param prepare fills in the rest when the file's turn comes, and may write the file then
     * @param done takes the stored file; not called when prepare refuses or the queue stops
     */
    virtual void QueueFile(const std::string &path, const PrepareFile &prepare,
                           const Done &done) = 0;
    /** Sends the queued requests, many at once, until all are done or stop says so. */
    virtual void RunQueue(const Stop &stop) = 0;
    /** How many queued requests are being sent right now. */
    virtual unsigned int Running() const = 0;

    /** Whether the cloud has refused an upload for lack of room since the queue last ran. */
    virtual bool IsFull() const = 0;
    /** The likely cause of the last failed request. */
    virtual Failure LastFailure() const = 0;
};
