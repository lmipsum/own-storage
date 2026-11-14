// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/GameName.h"

#include <cstddef>

namespace
{
const std::size_t kTitleIdLength = 9;
// The longest title a PARAM.SFO holds; the system dialog refuses a text that is too long.
const std::size_t kMaxTitleLength = 128;

/** The text without spaces at either end. */
std::string Trim(const std::string &text)
{
    const std::size_t first = text.find_first_not_of(' ');
    return first == std::string::npos ? std::string()
                                      : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

/** A title without control characters or spaces around it, no longer than a PARAM.SFO's. */
std::string ShownTitle(const std::string &title)
{
    std::string shown;
    for (const char c : title)
    {
        if (static_cast<unsigned char>(c) >= ' ' && c != '\x7f')
        {
            shown += c;
        }
    }
    // Some games start their titles with spaces, which would put them first in a sorted list.
    shown = Trim(shown);
    if (shown.size() <= kMaxTitleLength)
    {
        return shown;
    }
    std::size_t end = kMaxTitleLength;
    // Bytes 10xxxxxx continue a character; the cut goes before the byte that starts it.
    while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80)
    {
        --end;
    }
    return Trim(shown.substr(0, end));
}
} // namespace

std::string TitleId(const std::string &save)
{
    return save.substr(0, kTitleIdLength);
}

std::string GameName(const std::string &titleId, const std::string &title)
{
    const std::string shown = ShownTitle(title);
    return shown.empty() ? titleId : shown;
}

std::string GameLabel(const std::string &titleId, const std::string &title)
{
    const std::string shown = ShownTitle(title);
    return shown.empty() ? titleId : shown + " (" + titleId + ")";
}
