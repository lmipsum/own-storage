// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CloudSaveText.h"

#include <iomanip>
#include <sstream>

namespace
{
// Drive's "2026-11-13T21:13:21.672Z": the date, then the time from this offset.
const std::size_t kDateLength = 10;
const std::size_t kTimeOffset = 11;
const std::size_t kTimeLength = 5;
const double kBytesPerKilobyte = 1024.0;
} // namespace

std::string ShownSize(unsigned long long bytes)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(1);
    const double kilobytes = bytes / kBytesPerKilobyte;
    if (kilobytes < kBytesPerKilobyte)
    {
        text << kilobytes << " KB";
    }
    else
    {
        text << kilobytes / kBytesPerKilobyte << " MB";
    }
    return text.str();
}

std::string ShownTime(const std::string &modified)
{
    if (modified.size() < kTimeOffset + kTimeLength)
    {
        return modified;
    }
    return modified.substr(0, kDateLength) + " " + modified.substr(kTimeOffset, kTimeLength) +
           " UTC";
}

const char *ShownStatus(SaveStatus status)
{
    switch (status)
    {
    case SaveStatus::Missing:
        return "not on this PS3";
    case SaveStatus::Same:
        return "on this PS3";
    default:
        return "different on this PS3";
    }
}

std::string ShownResult(const std::string &name, LoadResult result)
{
    switch (result)
    {
    case LoadResult::Loaded:
        return name + " is loaded.";
    case LoadResult::AlreadyThere:
        return name + " is on this PS3 already.";
    case LoadResult::Kept:
        return name + " stays as it is on this PS3.";
    default:
        return name + " could not be loaded.";
    }
}
