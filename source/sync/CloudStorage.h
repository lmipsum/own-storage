// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"

#include <functional>
#include <string>

/** The type Drive gives a folder; remote.json entries carry it too. */
const std::string folderMime = "application/vnd.google-apps.folder";
/** The type a save file is sent with. */
const std::string octetMime = "application/octet-stream";
/** The type remote.json is sent with. */
const std::string jsonMime = "application/json";

/** What FindInFolder looks for. */
enum class ItemKind
{
    Folder,
    File
};

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

/** A folder to create, as decided when its turn in the queue comes. */
struct FolderToCreate
{
    std::string title;
    /** The folder to create it in. */
    std::string parentId;
};

/** A local file to store, as decided when its turn in the queue comes. */
struct FileToStore
{
    std::string title;
    /** The folder that holds it. */
    std::string parentId;
    /** The stored file to update; empty to create a new one. */
    std::string fileId;
};

/**
 * Where the backup is kept, as the sync needs it: searches, folders and files one by one, and a
 * queue that sends many at once. Google Drive is one.
 */
class CloudStorage
{
  public:
    /** Fills in a queued folder when its turn comes; false when nothing is to be sent. */
    typedef std::function<bool(FolderToCreate &folder)> PrepareFolder;
    /** Fills in a queued file when its turn comes; false when nothing is to be sent. */
    typedef std::function<bool(FileToStore &file)> PrepareFile;
    /** Takes what the cloud says about the item; without "id" when the request failed. */
    typedef std::function<void(const Json &item)> Done;
    /** Asked between requests; true lets the running ones finish and drops the rest. */
    typedef std::function<bool()> Stop;

    virtual ~CloudStorage() = default;

    /**
     * The folder or file of a title in a folder, unless it is trashed.
     * @param title the item's title
     * @param kind whether a folder or a file is looked for
     * @param parentId the folder that holds it
     * @param item receives the item; without "id" when there is none
     * @return false when the search failed, so whether the item exists is not known
     */
    virtual bool FindInFolder(const std::string &title, ItemKind kind, const std::string &parentId,
                              Json &item) = 0;
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
     * @param title its name in the cloud
     * @param mimeType its type
     * @param parentId the folder that holds it
     * @param fileId the stored file to update; empty to create a new one
     * @return the stored file; without "id" when the upload failed
     */
    virtual Json UploadFile(const std::string &path, const std::string &title,
                            const std::string &mimeType, const std::string &parentId,
                            const std::string &fileId) = 0;
    /**
     * Saves a stored file to a local path.
     * @param fileId the stored file
     * @param localPath where to save it
     * @return false when the download failed
     */
    virtual bool DownloadFile(const std::string &fileId, const std::string &localPath) = 0;

    /** Queues the creation of a folder; nothing is sent before RunQueue(). */
    virtual void QueueFolder(const PrepareFolder &prepare, const Done &done) = 0;
    /**
     * Queues storing a file; nothing is sent before RunQueue().
     * @param path the local file
     * @param prepare fills in the rest when the file is about to be sent
     * @param done takes the stored file
     */
    virtual void QueueFile(const std::string &path, const PrepareFile &prepare,
                           const Done &done) = 0;
    /** Sends the queued requests, many at once, until all are done or stop says so. */
    virtual void RunQueue(const Stop &stop) = 0;
    /** How many queued requests are being sent right now. */
    virtual unsigned int Running() const = 0;

    /** Whether the cloud has refused an upload for lack of room. */
    virtual bool IsFull() const = 0;
    /** The likely cause of the last failed request. */
    virtual Failure LastFailure() const = 0;
};
