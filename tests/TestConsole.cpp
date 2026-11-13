// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestConsole.h"

#include "ContentDigest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace
{
// The console paths the sync uses all start here.
const char kConsoleRoot[] = "/dev_hdd0";

/** Creates every missing directory on the way to path. */
void MakePcDirectories(const std::string &path)
{
    for (std::size_t slash = path.find('/', 1); slash != std::string::npos;
         slash = path.find('/', slash + 1))
    {
        mkdir(path.substr(0, slash).c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

/** Deletes a directory with everything in it. */
void RemoveTree(const std::string &path)
{
    if (DIR *directory = opendir(path.c_str()))
    {
        while (dirent *entry = readdir(directory))
        {
            const std::string name = entry->d_name;
            if (name != "." && name != "..")
            {
                RemoveTree(path + "/" + name);
            }
        }
        closedir(directory);
        rmdir(path.c_str());
        return;
    }
    std::remove(path.c_str());
}
} // namespace

TestConsole::TestConsole()
{
    char pattern[] = "/tmp/own-storage-test-XXXXXX";
    if (mkdtemp(pattern) == nullptr)
    {
        throw std::runtime_error("cannot create a temporary directory");
    }
    m_root = pattern;
    m_scratch = m_root + "/scratch";
    MakePcDirectories(m_scratch);
}

TestConsole::~TestConsole()
{
    RemoveTree(m_root);
}

std::string TestConsole::Map(const std::string &path) const
{
    return path.compare(0, sizeof kConsoleRoot - 1, kConsoleRoot) == 0 ? m_root + path : path;
}

void TestConsole::Put(const std::string &path, const std::string &content, std::time_t changed)
{
    const std::string mapped = Map(path);
    MakePcDirectories(mapped.substr(0, mapped.rfind('/')));
    std::ofstream(mapped.c_str(), std::ios::binary) << content;
    const utimbuf times = {changed, changed};
    utime(mapped.c_str(), &times);
}

std::string TestConsole::Content(const std::string &path) const
{
    std::string content;
    Read(path, content);
    return content;
}

void TestConsole::MakeDirectories(const std::string &path)
{
    MakePcDirectories(Map(path));
}

void TestConsole::RemoveDirectory(const std::string &path)
{
    rmdir(Map(path).c_str());
}

const std::string &TestConsole::Scratch() const
{
    return m_scratch;
}

std::vector<std::string> TestConsole::List(const std::string &directory) const
{
    std::vector<std::string> names;
    if (DIR *opened = opendir(Map(directory).c_str()))
    {
        while (dirent *entry = readdir(opened))
        {
            const std::string name = entry->d_name;
            if (name != "." && name != "..")
            {
                names.push_back(name);
            }
        }
        closedir(opened);
    }
    // The console lists in its own order; sorted, the tests do not depend on the PC's.
    std::sort(names.begin(), names.end());
    return names;
}

bool TestConsole::Exists(const std::string &path) const
{
    struct stat entry;
    return stat(Map(path).c_str(), &entry) == 0;
}

bool TestConsole::Status(const std::string &path, FileStatus &status) const
{
    struct stat entry;
    if (stat(Map(path).c_str(), &entry) != 0)
    {
        return false;
    }
    status.size = static_cast<unsigned long long>(entry.st_size);
    status.modified = entry.st_mtime;
    return true;
}

std::string TestConsole::Md5(const std::string &path) const
{
    std::string content;
    return Read(path, content) ? ContentDigest(content) : std::string();
}

bool TestConsole::Read(const std::string &path, std::string &content) const
{
    std::ifstream file(Map(path).c_str(), std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream read;
    read << file.rdbuf();
    content = read.str();
    return true;
}

bool TestConsole::Write(const std::string &path, const std::string &content)
{
    std::ofstream file(Map(path).c_str(), std::ios::binary | std::ios::trunc);
    return static_cast<bool>(file.write(content.data(), content.size()));
}

void TestConsole::Remove(const std::string &path)
{
    std::remove(Map(path).c_str());
}

void TestConsole::MakeDirectory(const std::string &path)
{
    mkdir(Map(path).c_str(), 0755);
}
