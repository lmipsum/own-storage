// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Files.h"

#include "app/DebugLog.h"

#include <fstream>
#include <sstream>

#include <sys/file.h>

namespace
{
const int kStateFileMode = 0644;
} // namespace

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

bool SaveStateFile(const Json &content, const std::string &path)
{
    if (!content.Save(path))
    {
        debugPrintf("SaveStateFile: could not write %s\n", path.c_str());
        return false;
    }
    sysFsChmod(path.c_str(), kStateFileMode);
    return true;
}

Json LoadWithBackup(const std::string &path, const std::string &backupPath)
{
    const Json content = Json::Load(path);
    if (!content.IsEmpty())
    {
        return content;
    }
    // Every save writes the two files one after the other, so a power cut spoils at most one.
    debugPrintf("LoadWithBackup: %s is missing or damaged, reading %s\n", path.c_str(),
                backupPath.c_str());
    const Json backup = Json::Load(backupPath);
    if (!backup.IsEmpty())
    {
        // Left spoiled until the next save, the file would leave the backup as the only copy.
        SaveStateFile(backup, path);
    }
    return backup;
}
