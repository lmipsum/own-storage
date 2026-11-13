// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleStorage.h"

#include <stdio.h>
#include <cstring>

#include <mbedtls/md.h>
#include <sys/file.h>

namespace
{
// The access a created directory gets: the owner writes, everyone reads.
const s32 kDirectoryMode = 0755;
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

bool ConsoleStorage::Exists(const std::string &path) const
{
    sysFSStat entry;
    return sysFsStat(path.c_str(), &entry) == 0;
}

bool ConsoleStorage::ModifiedTime(const std::string &path, std::time_t &time) const
{
    sysFSStat entry;
    if (sysFsStat(path.c_str(), &entry) != 0)
    {
        return false;
    }
    time = entry.st_mtime;
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

void ConsoleStorage::MakeDirectory(const std::string &path)
{
    sysFsMkdir(path.c_str(), kDirectoryMode);
}
