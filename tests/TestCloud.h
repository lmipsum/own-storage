// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/CloudStorage.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

/** One folder or file in the test cloud. */
struct TestCloudItem
{
    std::string title;
    std::string parentId;
    std::string mimeType;
    std::string content;
};

/**
 * A cloud in memory that answers at once and sends its queue one request after the other. It
 * can fail searches and lose the answer to a create, as a real one sometimes does.
 */
class TestCloud : public CloudStorage
{
  public:
    /** Reads and writes local files where the sync names them. */
    typedef std::function<std::string(const std::string &path)> PathMap;

    explicit TestCloud(const PathMap &map);

    /** Makes every search fail from now on, or work again. */
    void FailSearches(bool fail);
    /** The create after this many more creates is done, but its answer is lost. */
    void LoseAnswerOfCreate(int createsBefore);
    /** How many folders and files were created. */
    int Creates() const;
    /** How many stored files were overwritten. */
    int Updates() const;
    /** Every item, by id. */
    const std::map<std::string, TestCloudItem> &Items() const;
    /**
     * The titles under a folder, one per line, indented by depth and sorted; a folder ends in /.
     * @param parentId the folder
     * @return the listing
     */
    std::string Tree(const std::string &parentId) const;
    /** Stores a file directly, as another program would. */
    std::string Store(const TestCloudItem &item);

    bool FindInFolder(const std::string &title, ItemKind kind, const std::string &parentId,
                      Json &item) override;
    Json CreateFolder(const std::string &title, const std::string &parentId) override;
    Json UploadFile(const std::string &path, const std::string &title, const std::string &mimeType,
                    const std::string &parentId, const std::string &fileId) override;
    bool DownloadFile(const std::string &fileId, const std::string &localPath) override;
    void QueueFolder(const PrepareFolder &prepare, const Done &done) override;
    void QueueFile(const std::string &path, const PrepareFile &prepare, const Done &done) override;
    void RunQueue(const Stop &stop) override;
    unsigned int Running() const override;
    bool IsFull() const override;
    Failure LastFailure() const override;

  private:
    /** What the cloud answers about an item. */
    Json Describe(const std::string &id) const;
    /** Adds an item; its answer is lost when LoseAnswerOfCreate() says so. */
    Json Create(const TestCloudItem &item);

    PathMap m_map;
    std::map<std::string, TestCloudItem> m_items;
    std::vector<std::function<void()>> m_queue;
    bool m_failSearches;
    int m_createsUntilLostAnswer;
    int m_creates;
    int m_updates;
    int m_nextId;
};
