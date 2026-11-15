// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbInstall.h"

#include "app/DebugLog.h"

#include <cstdint>
#include <ctime>

#include <ppu-lv2.h>
#include <sys/file.h>

namespace
{
// The XMB's Game column, as the system reads it and as a writable mount of the flash shows it.
const char kCategoryFile[] = "/vsh/resource/explore/xmb/category_game.xml";
const char kSystemFlash[] = "/dev_flash";
// Where CFW and HEN show the flash writable, in the order webMAN MOD's installer tries them.
const char *const kWritableFlashes[] = {"/dev_blind", "/dev_habib", "/dev_rewrite"};
// Names of the app's own, so a backup another tool keeps as category_game.xml.bak stays as it is.
const char kBackupSuffix[] = ".owns.bak";
const char kNewSuffix[] = ".owns.new";
// sys_fs_mount and sys_fs_unmount, which make the flash writable at /dev_blind and undo it.
const u64 kSysFsMount = 837;
const u64 kSysFsUnmount = 838;
// An action webMAN wrote this long ago did not start the app, so it is not done any more.
const std::time_t kActionLifetimeSeconds = 600;

/** A text's address, as a system call takes it. */
u64 Address(const char *text)
{
    return static_cast<u64>(reinterpret_cast<std::uintptr_t>(text));
}

/** Creates each folder of a path that ends in a slash, as a console without webMAN may lack them.
 */
void MakeDirectories(LocalStorage &storage, const std::string &path)
{
    for (std::size_t slash = path.find('/', 1); slash != std::string::npos;
         slash = path.find('/', slash + 1))
    {
        storage.MakeDirectory(path.substr(0, slash));
    }
}

/** Whether a file is there. */
bool Exists(const LocalStorage &storage, const std::string &path)
{
    FileStatus status;
    return storage.Status(path, status);
}

/**
 * category_game.xml through a writable mount of the flash.
 * @param mounted set when this app mounted the flash, which it then unmounts again
 * @return the path; empty when the flash cannot be written
 */
std::string WritableCategory(const LocalStorage &storage, bool &mounted)
{
    mounted = false;
    for (const char *flash : kWritableFlashes)
    {
        if (Exists(storage, std::string(flash) + kCategoryFile))
        {
            return std::string(flash) + kCategoryFile;
        }
    }
    lv2syscall8(kSysFsMount, Address("CELL_FS_IOS:BUILTIN_FLSH1"), Address("CELL_FS_FAT"),
                Address(kWritableFlashes[0]), 0, 0, 0, 0, 0);
    const std::string path = std::string(kWritableFlashes[0]) + kCategoryFile;
    mounted = Exists(storage, path);
    return mounted ? path : std::string();
}

/**
 * Replaces a file of the flash so that the system's file is never missing: the new one is
 * written and read back before the old one is moved aside, and only renames happen after that.
 * @return false when the old file is still in place
 */
bool ReplaceSafely(LocalStorage &storage, const std::string &path, const std::string &content)
{
    const std::string fresh = path + kNewSuffix;
    const std::string backup = path + kBackupSuffix;
    std::string written;
    if (!storage.Write(fresh, content) || !storage.Read(fresh, written) || written != content)
    {
        storage.Remove(fresh);
        debugPrintf("InstallXmbFolder: the new Game column's file cannot be written\n");
        return false;
    }
    storage.Remove(backup);
    if (sysLv2FsRename(path.c_str(), backup.c_str()) != 0)
    {
        storage.Remove(fresh);
        debugPrintf("InstallXmbFolder: the Game column's file cannot be kept\n");
        return false;
    }
    if (sysLv2FsRename(fresh.c_str(), path.c_str()) != 0)
    {
        const bool restored = sysLv2FsRename(backup.c_str(), path.c_str()) == 0;
        debugPrintf("InstallXmbFolder: the new file cannot take the old one's place; %s\n",
                    restored ? "put back" : "NOT put back");
        return false;
    }
    return true;
}
} // namespace

bool InstallXmbFolder(LocalStorage &storage, const XmbPlace &place)
{
    std::string category;
    if (!storage.Read(std::string(kSystemFlash) + kCategoryFile, category))
    {
        debugPrintf("InstallXmbFolder: the Game column's file cannot be read\n");
        return false;
    }
    if (!AddXmbCategoryEntry(category, XmbCategoryEntry(place)))
    {
        return true;
    }
    bool mounted = false;
    const std::string path = WritableCategory(storage, mounted);
    if (path.empty())
    {
        debugPrintf("InstallXmbFolder: the flash cannot be written\n");
        return false;
    }
    const bool installed = ReplaceSafely(storage, path, category);
    if (mounted)
    {
        // The flash stays as the system had it: read-only.
        lv2syscall3(kSysFsUnmount, Address(kWritableFlashes[0]), 0, 0);
    }
    if (installed)
    {
        debugPrintf("InstallXmbFolder: the Own Storage folder is in the Game column\n");
    }
    return installed;
}

bool WriteXmbLists(LocalStorage &storage, const XmbPlace &place, const XmbLists &lists)
{
    MakeDirectories(storage, place.directory);
    MakeDirectories(storage, place.scriptDirectory);
    // The lists go first: should a script then fail, a row starts nothing rather than the action
    // of a row of the last lists that had its key.
    bool written = storage.Write(place.directory + xmbFolderFile, lists.folder) &&
                   storage.Write(place.directory + xmbMenuFile, lists.menu) &&
                   storage.Write(place.directory + xmbSavesFile, lists.saves) &&
                   storage.Write(place.directory + xmbGamesFile, lists.games);
    // The rows of the last lists, such as of a save gone from the cloud, start nothing any more.
    for (const std::string &old : storage.List(place.scriptDirectory))
    {
        storage.Remove(place.scriptDirectory + old);
    }
    for (const XmbScript &script : lists.scripts)
    {
        written = written && storage.Write(place.scriptDirectory + script.key + xmbScriptSuffix,
                                           XmbScriptText(place, script));
    }
    if (!written)
    {
        debugPrintf("WriteXmbLists: the lists cannot be written\n");
    }
    return written;
}

std::string TakeXmbAction(LocalStorage &storage, const XmbPlace &place)
{
    FileStatus status;
    std::string action;
    if (!storage.Status(place.actionFile, status) || !storage.Read(place.actionFile, action))
    {
        return std::string();
    }
    storage.Remove(place.actionFile);
    if (std::time(nullptr) - status.modified > kActionLifetimeSeconds)
    {
        debugPrintf("TakeXmbAction: an old action is left undone\n");
        return std::string();
    }
    // The script's "log" ends the action with a line end, which is no part of it.
    while (!action.empty() && (action.back() == '\n' || action.back() == '\r'))
    {
        action.pop_back();
    }
    return action;
}
