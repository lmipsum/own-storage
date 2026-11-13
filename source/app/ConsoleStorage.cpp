// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleStorage.h"

#include "app/Files.h"

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

void ConsoleStorage::Remove(const std::string &path)
{
    sysFsUnlink(path.c_str());
}

void ConsoleStorage::MakeDirectory(const std::string &path)
{
    sysFsMkdir(path.c_str(), kDirectoryMode);
}
