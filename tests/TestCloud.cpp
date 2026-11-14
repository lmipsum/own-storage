// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestCloud.h"

#include "ContentDigest.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
/** Now, in the form Drive gives modifiedDate. */
std::string Now()
{
    const std::time_t now = std::time(nullptr);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.000Z", std::gmtime(&now));
    return text;
}
} // namespace

TestCloud::TestCloud()
    : m_failSearches(false), m_damageDownloads(false), m_createsUntilLostAnswer(-1), m_creates(0),
      m_updates(0), m_nextId(1)
{
}

void TestCloud::FailSearches(bool fail)
{
    m_failSearches = fail;
}

void TestCloud::DamageDownloads(bool damage)
{
    m_damageDownloads = damage;
}

void TestCloud::LoseAnswerOfCreate(int createsBefore)
{
    m_createsUntilLostAnswer = createsBefore;
}

int TestCloud::Creates() const
{
    return m_creates;
}

int TestCloud::Updates() const
{
    return m_updates;
}

std::string TestCloud::Description(const std::string &id) const
{
    const auto description = m_descriptions.find(id);
    return description == m_descriptions.end() ? std::string() : description->second;
}

void TestCloud::SetDescription(const std::string &id, const std::string &description)
{
    m_descriptions[id] = description;
}

bool TestCloud::CloudOnly(const std::string &id) const
{
    const auto mark = m_cloudOnly.find(id);
    return mark != m_cloudOnly.end() && mark->second;
}

void TestCloud::SetModified(const std::string &id, const std::string &modified)
{
    m_modified[id] = modified;
}

const std::map<std::string, TestCloudItem> &TestCloud::Items() const
{
    return m_items;
}

std::string TestCloud::Tree(const std::string &parentId) const
{
    std::map<std::string, std::string> children;
    for (const std::pair<const std::string, TestCloudItem> &item : m_items)
    {
        if (item.second.parentId == parentId)
        {
            children.insert(std::make_pair(item.second.title, item.first));
        }
    }
    std::string tree;
    for (const std::pair<const std::string, std::string> &child : children)
    {
        const TestCloudItem &item = m_items.at(child.second);
        if (item.mimeType == folderMime)
        {
            tree += item.title + "/\n";
            std::istringstream inner(Tree(child.second));
            for (std::string line; std::getline(inner, line);)
            {
                tree += "  " + line + "\n";
            }
        }
        else
        {
            tree += item.title + "\n";
        }
    }
    return tree;
}

std::string TestCloud::Store(const TestCloudItem &item)
{
    char id[16];
    std::snprintf(id, sizeof id, "id%04d", m_nextId++);
    m_items[id] = item;
    return id;
}

void TestCloud::Delete(const std::string &id)
{
    m_items.erase(id);
}

void TestCloud::Store(const std::string &id, const TestCloudItem &item)
{
    m_items[id] = item;
}

bool TestCloud::FindFolder(const std::string &title, const std::string &parentId, Json &folder)
{
    folder = Json();
    if (m_failSearches)
    {
        return false;
    }
    for (const std::pair<const std::string, TestCloudItem> &stored : m_items)
    {
        if (stored.second.title == title && stored.second.parentId == parentId &&
            stored.second.mimeType == folderMime)
        {
            folder = Describe(stored.first);
            break;
        }
    }
    return true;
}

bool TestCloud::ListFiles(const std::string &folderId, Json::Array &files)
{
    files.clear();
    if (m_failSearches)
    {
        return false;
    }
    for (const std::pair<const std::string, TestCloudItem> &stored : m_items)
    {
        if (stored.second.parentId == folderId && stored.second.mimeType != folderMime)
        {
            files.push_back(Describe(stored.first));
        }
    }
    return true;
}

Json TestCloud::CreateFolder(const std::string &title, const std::string &parentId)
{
    return Create(TestCloudItem{title, parentId, folderMime, ""}, std::string(), false);
}

Json TestCloud::UploadFile(const std::string &path, const FileToStore &file,
                           const std::string &mimeType)
{
    std::ifstream local(path.c_str(), std::ios::binary);
    if (!local)
    {
        return Json();
    }
    std::ostringstream content;
    content << local.rdbuf();
    if (file.fileId.empty())
    {
        return Create(TestCloudItem{file.title, file.parentId, mimeType, content.str()},
                      file.description, file.cloudOnly);
    }
    if (m_items.count(file.fileId) == 0)
    {
        return Json();
    }
    m_items[file.fileId].content = content.str();
    m_descriptions[file.fileId] = file.description;
    m_cloudOnly[file.fileId] = file.cloudOnly;
    ++m_updates;
    return Describe(file.fileId);
}

bool TestCloud::TrashFile(const std::string &fileId)
{
    m_descriptions.erase(fileId);
    m_cloudOnly.erase(fileId);
    return m_items.erase(fileId) == 1;
}

bool TestCloud::DownloadFile(const std::string &fileId, const std::string &localPath)
{
    if (m_items.count(fileId) == 0)
    {
        return false;
    }
    std::ofstream(localPath.c_str(), std::ios::binary)
        << m_items[fileId].content << (m_damageDownloads ? "x" : "");
    return true;
}

void TestCloud::QueueFile(const std::string &path, const PrepareFile &prepare, const Done &done)
{
    m_queue.push_back(
        [this, path, prepare, done]
        {
            FileToStore file{};
            if (prepare(file))
            {
                done(UploadFile(path, file, octetMime));
            }
        });
}

void TestCloud::RunQueue(const Stop &stop)
{
    std::vector<std::function<void()>> queue;
    queue.swap(m_queue);
    for (const std::function<void()> &request : queue)
    {
        if (stop())
        {
            break;
        }
        request();
    }
}

unsigned int TestCloud::Running() const
{
    return 0;
}

bool TestCloud::IsFull() const
{
    return false;
}

Failure TestCloud::LastFailure() const
{
    return Failure::Refused;
}

Json TestCloud::Describe(const std::string &id) const
{
    const TestCloudItem &item = m_items.at(id);
    Json parent;
    parent.Add("id", Json(item.parentId));
    Json described;
    described.Add("id", Json(id));
    described.Add("title", Json(item.title));
    described.Add("mimeType", Json(item.mimeType));
    described.Add("parents", Json(Json::Array{parent}));
    const auto modified = m_modified.find(id);
    described.Add("modifiedDate", Json(modified == m_modified.end() ? Now() : modified->second));
    if (item.mimeType != folderMime)
    {
        described.Add("description", Json(Description(id)));
        // Drive gives no properties for a file that never got one, as one stored directly.
        if (m_cloudOnly.count(id) != 0)
        {
            Json mark;
            mark.Add("key", Json(cloudOnlyProperty));
            mark.Add("value", Json(CloudOnly(id) ? cloudOnlyMarked : cloudOnlyUnmarked));
            described.Add("properties", Json(Json::Array{mark}));
        }
        described.Add("md5Checksum", Json(ContentDigest(item.content)));
        described.Add("fileSize", Json(std::to_string(item.content.size())));
    }
    return described;
}

Json TestCloud::Create(const TestCloudItem &item, const std::string &description, bool cloudOnly)
{
    const std::string id = Store(item);
    m_descriptions[id] = description;
    m_cloudOnly[id] = cloudOnly;
    ++m_creates;
    if (m_createsUntilLostAnswer == 0)
    {
        m_createsUntilLostAnswer = -1;
        return Json();
    }
    if (m_createsUntilLostAnswer > 0)
    {
        --m_createsUntilLostAnswer;
    }
    return Describe(id);
}
