// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Config.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "app/Paths.h"
#include "json/Json.h"

namespace
{
const std::string kConfigFile = APP_DIR "config.json";
const std::string kConfigBackup = APP_DIR "config.backup";
} // namespace

bool LoadConfig(Config &config)
{
    const Json saved = LoadWithBackup(kConfigFile, kConfigBackup);
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

void SaveConfig(const Config &config)
{
    Json saved;
    saved.Add("refresh_token", Json(config.refreshToken));
    saved.Add("root_id", Json(config.rootFolderId));
    SaveStateFile(saved, kConfigFile);
    SaveStateFile(saved, kConfigBackup);
}
