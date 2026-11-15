// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/LauncherSetup.h"

#include "app/DebugLog.h"

namespace
{
// webMAN MOD's installer ends its lines so; Cobra reads either end.
const char kLineEnd[] = "\r\n";

/** Whether a boot list names the path on a line of its own. */
bool HasLine(const std::string &list, const std::string &path)
{
    std::size_t start = 0;
    while (start <= list.size())
    {
        std::size_t end = list.find('\n', start);
        if (end == std::string::npos)
        {
            end = list.size();
        }
        std::string line = list.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        {
            line.pop_back();
        }
        if (line == path)
        {
            return true;
        }
        start = end + 1;
    }
    return false;
}

/** Copies the bundled plugin over the installed one, unless they are the same file already. */
bool CopyPlugin(LocalStorage &storage, const LauncherPlace &place, bool &changed)
{
    const std::string bundled = storage.Md5(place.bundled);
    if (bundled.empty())
    {
        debugPrintf("SetUpLauncher: the app brings no launcher\n");
        return false;
    }
    if (storage.Md5(place.installed) == bundled)
    {
        return true;
    }
    std::string plugin;
    const std::size_t folder = place.installed.rfind('/');
    if (folder != std::string::npos)
    {
        storage.MakeDirectory(place.installed.substr(0, folder));
    }
    if (!storage.Read(place.bundled, plugin) || !storage.Write(place.installed, plugin))
    {
        debugPrintf("SetUpLauncher: the launcher cannot be copied\n");
        return false;
    }
    changed = true;
    return true;
}

/** Adds the plugin's line to the boot list, unless it has it. */
bool AddBootLine(LocalStorage &storage, const LauncherPlace &place, bool &changed)
{
    std::string list;
    FileStatus status = {0, 0};
    const bool listed = storage.Status(place.bootList, status);
    if (listed && !storage.Read(place.bootList, list))
    {
        debugPrintf("SetUpLauncher: the boot list cannot be read\n");
        return false;
    }
    const std::string updated = WithBootLine(list, place.installed);
    if (updated == list)
    {
        return true;
    }
    FileStatus backup = {0, 0};
    if (listed && !storage.Status(place.bootListBackup, backup) &&
        !storage.Write(place.bootListBackup, list))
    {
        debugPrintf("SetUpLauncher: the boot list cannot be kept\n");
        return false;
    }
    if (!storage.Write(place.bootList, updated))
    {
        debugPrintf("SetUpLauncher: the boot list cannot be written\n");
        return false;
    }
    changed = true;
    return true;
}
} // namespace

std::string WithBootLine(const std::string &list, const std::string &path)
{
    if (HasLine(list, path))
    {
        return list;
    }
    const bool ended = list.empty() || list.back() == '\n';
    return list + (ended ? "" : kLineEnd) + path + kLineEnd;
}

LauncherSetup SetUpLauncher(LocalStorage &storage, const LauncherPlace &place)
{
    bool changed = false;
    if (!CopyPlugin(storage, place, changed) || !AddBootLine(storage, place, changed))
    {
        return LauncherSetup::Failed;
    }
    if (changed)
    {
        debugPrintf("SetUpLauncher: the launcher runs from the next start\n");
    }
    return changed ? LauncherSetup::Changed : LauncherSetup::Unchanged;
}
