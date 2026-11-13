// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Files.h"

#include "app/DebugLog.h"

#include <fstream>
#include <sstream>

#include <sys/file.h>

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

/**
 * Writes a state file and makes it readable to all (0644).
 * @param content the state to write
 * @param path the file to write it to
 * @return false when the file could not be written
 */
bool saveStateFile(const Json &content, const std::string &path)
{
    if (!content.Save(path))
    {
        debugPrintf("saveStateFile: could not write %s\n", path.c_str());
        return false;
    }
    sysFsChmod(path.c_str(), 0644);
    return true;
}

/**
 * Reads a state file, or its backup when the file is missing or cannot be parsed, and then
 * writes the backup's content over the spoiled file.
 * @param path the state file
 * @param backupPath the copy that every save writes next to it
 * @return the content; empty when neither can be read
 */
Json loadWithBackup(const std::string &path, const std::string &backupPath)
{
    const Json content = Json::Load(path);
    if (!content.IsEmpty())
    {
        return content;
    }
    // Every save writes the two files one after the other, so a power cut spoils at most one.
    debugPrintf("%s is missing or damaged, reading %s\n", path.c_str(), backupPath.c_str());
    const Json backup = Json::Load(backupPath);
    if (!backup.IsEmpty())
    {
        // Left spoiled until the next save, the file would leave the backup as the only copy.
        saveStateFile(backup, path);
    }
    return backup;
}
