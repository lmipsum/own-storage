// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleStorage.h"

#include "app/Files.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdio.h>

#include <mbedtls/md.h>
#include <sys/file.h>

namespace
{
// The access a created directory gets: the owner writes, everyone reads.
const s32 kDirectoryMode = 0755;
// The access a written file gets: the owner writes, everyone reads.
const s32 kFileMode = 0644;
// How many directory entries one call reads; a save folder rarely has more files.
const std::size_t kDirectoryBatch = 32;

/**
 * A directory entry as the firmware writes it (CellFsDirectoryEntry): PSL1GHT's
 * sysFSDirectoryEntry has room for a whole path in the name, so its entries do not line up with
 * the firmware's.
 */
struct __attribute__((packed)) FirmwareDirectoryEntry
{
    sysFSStat attribute;
    u8 type;
    u8 nameLength;
    char name[256];
};
static_assert(sizeof(FirmwareDirectoryEntry) == 310, "CellFsDirectoryEntry is 310 bytes");
} // namespace

std::vector<std::string> ConsoleStorage::List(const std::string &directory) const
{
    std::vector<std::string> names;
    s32 fd;
    if (sysFsOpendir(directory.c_str(), &fd) != 0)
    {
        return names;
    }
    sysFSDirent entry;
    u64 read;
    while (sysFsReaddir(fd, &entry, &read) == 0 && read > 0 && std::strlen(entry.d_name) > 0)
    {
        if (std::strcmp(entry.d_name, ".") != 0 && std::strcmp(entry.d_name, "..") != 0)
        {
            names.push_back(entry.d_name);
        }
    }
    sysFsClosedir(fd);
    return names;
}

std::vector<DirectoryEntry> ConsoleStorage::ListWithStatus(const std::string &directory) const
{
    std::vector<DirectoryEntry> entries;
    s32 fd;
    if (sysFsOpendir(directory.c_str(), &fd) != 0)
    {
        return entries;
    }
    // The system hands the entries over a batch at a time, each with its file's status, rather
    // than one call for every file's status.
    std::vector<FirmwareDirectoryEntry> batch(kDirectoryBatch);
    u32 count = 0;
    while (sysFsGetDirectoryEntries(fd, reinterpret_cast<sysFSDirectoryEntry *>(batch.data()),
                                    batch.size() * sizeof(FirmwareDirectoryEntry), &count) == 0 &&
           count > 0)
    {
        for (u32 i = 0; i < count && i < batch.size(); ++i)
        {
            // The firmware does not end a name that fills its field.
            const char *start = batch[i].name;
            const char *end = std::find(start, start + sizeof batch[i].name, '\0');
            const std::string name(start, end);
            if (name != "." && name != "..")
            {
                entries.push_back(DirectoryEntry{
                    name, {batch[i].attribute.st_size, batch[i].attribute.st_mtime}});
            }
        }
    }
    sysFsClosedir(fd);
    return entries;
}

bool ConsoleStorage::Status(const std::string &path, FileStatus &status) const
{
    sysFSStat entry;
    if (sysFsStat(path.c_str(), &entry) != 0)
    {
        return false;
    }
    status.size = entry.st_size;
    status.modified = entry.st_mtime;
    return true;
}

std::string ConsoleStorage::Md5(const std::string &path) const
{
    unsigned char digest[16];
    if (mbedtls_md_file(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), path.c_str(), digest) != 0)
    {
        return "";
    }
    std::string hex;
    char digits[3];
    for (unsigned char byte : digest)
    {
        // The console's C++ library offers snprintf only outside std.
        snprintf(digits, sizeof digits, "%02x", byte);
        hex += digits;
    }
    return hex;
}

bool ConsoleStorage::Read(const std::string &path, std::string &content) const
{
    return ReadWholeFile(path, content);
}

bool ConsoleStorage::ReadPart(const std::string &path, unsigned long long offset, std::size_t size,
                              std::string &content) const
{
    std::ifstream file(path.c_str(), std::ios::binary);
    content.resize(size);
    return file.seekg(static_cast<std::streamoff>(offset)) &&
           file.read(&content[0], static_cast<std::streamsize>(size));
}

bool ConsoleStorage::Write(const std::string &path, const std::string &content)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file.write(content.data(), content.size());
    file.close();
    if (!file)
    {
        return false;
    }
    sysFsChmod(path.c_str(), kFileMode);
    return true;
}

bool ConsoleStorage::Append(const std::string &path, const std::string &content)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::app);
    file.write(content.data(), content.size());
    file.close();
    return static_cast<bool>(file);
}

void ConsoleStorage::Remove(const std::string &path)
{
    sysFsUnlink(path.c_str());
}

void ConsoleStorage::MakeDirectory(const std::string &path)
{
    sysFsMkdir(path.c_str(), kDirectoryMode);
}
