// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Files.h"

#include <fstream>
#include <sstream>

bool ReadWholeFile(const std::string &path, std::string &text)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream content;
    content << file.rdbuf();
    text = content.str();
    return true;
}
