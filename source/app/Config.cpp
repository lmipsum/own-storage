// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Config.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "json/Json.h"

namespace
{
const char kConfigFile[] = "/config.json";
const char kConfigBackup[] = "/config.backup";
} // namespace

bool LoadConfig(const std::string &directory, Config &config)
{
    const Json saved = LoadWithBackup(directory + kConfigFile, directory + kConfigBackup);
    if (saved.IsEmpty())
    {
        debugPrintf("LoadConfig: No readable config file\n");
        return false;
    }
    config.refreshToken = saved["refresh_token"].Str();
    if (saved.Has("root_id"))
    {
        config.rootFolderId = saved["root_id"].Str();
    }
    debugPrintf("LoadConfig: Config file read\n");
    return true;
}

void SaveConfig(const std::string &directory, const Config &config)
{
    Json saved;
    saved.Add("refresh_token", Json(config.refreshToken));
    saved.Add("root_id", Json(config.rootFolderId));
    SaveStateFile(saved, directory + kConfigFile);
    SaveStateFile(saved, directory + kConfigBackup);
}
