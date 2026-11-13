// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleStorage.h"

#include <cstring>

#include <sys/file.h>

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
