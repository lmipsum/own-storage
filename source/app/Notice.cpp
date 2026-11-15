// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Notice.h"

Notice Result(const std::string &text, const std::string &system)
{
    return Notice{text, system, false};
}

Notice Problem(const std::string &text)
{
    return Notice{text, std::string(), true};
}

std::string NotificationLine(const Notice &notice)
{
    std::string line = notice.system.empty() ? notice.text : notice.system;
    for (char &c : line)
    {
        if (c == '\n' || c == '\r')
        {
            c = ' ';
        }
    }
    return line;
}
