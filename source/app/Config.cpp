// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Config.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "app/Paths.h"
#include "auth/GoogleDeviceAuth.h"
#include "json/Json.h"

// TODO: main.cpp still owns these; a Config value takes them over (#24).
extern GoogleDeviceAuth googleAuth;
extern std::string rootFolderId;
extern std::string remoteJsonId;
extern std::string last_sync;

const std::string appFile = APP_DIR "config.json";
const std::string appFileBackup = APP_DIR "config.backup";

/**
 * Stores the current application configuration in the application config json
 */
void storeConfig()
{
    Json appConfig;
    appConfig.Add("refresh_token", Json(googleAuth.RefreshToken()));
    appConfig.Add("root_id", Json(rootFolderId));
    appConfig.Add("last_sync", Json(last_sync));
    appConfig.Add("remote_fid", Json(remoteJsonId));

    saveStateFile(appConfig, appFile);
    saveStateFile(appConfig, appFileBackup);
}

/**
 * Utility function that reads the config file for refresh token etc
 * @return "not_found" if file not found or corrupt else the refresh_token
 */
std::string isUserAuthenticated()
{
    std::string result = "not_found";
    Json config = loadWithBackup(appFile, appFileBackup);

    if (!config.IsEmpty())
    {
        debugPrintf("Config file not empty, returning refresh token...\n");
        result = config["refresh_token"].Str();
        rootFolderId = config.Has("root_id") ? config["root_id"].Str() : "root";
        last_sync = config.Has("last_sync") ? config["last_sync"].Str() : "0";
        remoteJsonId = config.Has("remote_fid") ? config["remote_fid"].Str() : "root";
    }
    else
    {
        debugPrintf("No readable config file, returning empty handed...\n");
    }

    return result;
}
