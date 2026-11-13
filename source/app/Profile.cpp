// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Profile.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "app/Paths.h"
#include "sync/SaveScanner.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#include <sys/memory.h>
#include <sys/systime.h>
#include <sysutil/save.h>

namespace
{
// The app's directory that holds one directory per user.
const char kProfilesDirectory[] = APP_DIR "users";
const char kMarkerFile[] = "MARKER.DAT";
const char kMarkerTitle[] = "Own Storage";
const char kMarkerSubtitle[] = "Tells Own Storage which user is logged in";
// The package's icon, which the system shows next to the marker save.
const char kIconFile[] = APP_DIR "../ICON0.PNG";
// The memory the save data utility works in, as the PSL1GHT save sample gives it.
const u32 kUtilityMemory = 5 * 1024 * 1024;
// The marker save holds the icon and the marker file.
const u32 kMarkerSaveFiles = 2;

/** What the save data utility's callbacks write, and how far they are. */
struct MarkerSave
{
    std::string marker;
    std::string icon;
    bool iconWritten;
    bool markerWritten;
};

/** Names the marker save and replaces whatever an earlier run left in it. */
void DescribeMarkerSave(sysSaveCallbackResult *result, sysSaveStatusIn *in, sysSaveStatusOut *out)
{
    std::strncpy(in->getParam.title, kMarkerTitle, SYS_SAVE_MAX_TITLE - 1);
    std::strncpy(in->getParam.subtitle, kMarkerSubtitle, SYS_SAVE_MAX_SUBTITLE - 1);
    in->getParam.detail[0] = '\0';
    out->setParam = &in->getParam;
    out->recreateMode = SYS_SAVE_RECREATE_MODE_DELETE;
    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
}

/** Writes the icon, then the marker file, one per call. */
void WriteMarkerSave(sysSaveCallbackResult *result, sysSaveFileIn *, sysSaveFileOut *out)
{
    MarkerSave *save = static_cast<MarkerSave *>(result->user_data);
    std::memset(out, 0, sizeof *out);
    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    if (!save->iconWritten && !save->icon.empty())
    {
        save->iconWritten = true;
        out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
        out->fileType = SYS_SAVE_FILETYPE_CONTENT_ICON0;
        out->size = out->bufferSize = save->icon.size();
        out->buffer = &save->icon[0];
        return;
    }
    if (!save->markerWritten)
    {
        save->markerWritten = true;
        out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
        out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
        out->filename = const_cast<char *>(kMarkerFile);
        out->size = out->bufferSize = save->marker.size();
        out->buffer = &save->marker[0];
        return;
    }
    result->result = SYS_SAVE_CALLBACK_RESULT_DONE;
}

/** A marker no earlier run has written: the time of this one to the nanosecond. */
std::string NewMarker()
{
    u64 seconds;
    u64 nanoseconds;
    sysGetCurrentTime(&seconds, &nanoseconds);
    std::ostringstream marker;
    marker << seconds << '.' << nanoseconds;
    return marker.str();
}

/** Saves the marker through the save data utility; false when it fails. */
bool SaveMarker(MarkerSave &save)
{
    sysSaveBufferSettings buffer;
    std::memset(&buffer, 0, sizeof buffer);
    buffer.maxDirectories = 1;
    buffer.maxFiles = kMarkerSaveFiles;
    // Room for the larger of the directory list and the file list, as the utility asks.
    std::vector<char> space(
        std::max(sizeof(sysSaveDirectoryList), kMarkerSaveFiles * sizeof(sysSaveFileStatus)));
    buffer.bufferSize = space.size();
    buffer.buffer = space.data();

    sys_mem_container_t container;
    if (sysMemContainerCreate(&container, kUtilityMemory) != 0)
    {
        debugPrintf("SaveMarker: no memory for the save data utility\n");
        return false;
    }
    const s32 status = sysSaveAutoSave2(SYS_SAVE_CURRENT_VERSION, profileMarkerSave.c_str(),
                                        SYS_SAVE_ERROR_DIALOG_NONE, &buffer, DescribeMarkerSave,
                                        WriteMarkerSave, container, &save);
    sysMemContainerDestroy(container);
    if (status != SYS_SAVE_RETURN_DONE)
    {
        debugPrintf("SaveMarker: the save data utility answered %08x\n",
                    static_cast<unsigned int>(status));
        return false;
    }
    return true;
}
} // namespace

std::optional<std::string> FindLoggedInUser(const LocalStorage &storage)
{
    MarkerSave save = {NewMarker(), std::string(), false, false};
    // Without the icon the marker save still works; the system then shows a blank one.
    ReadWholeFile(kIconFile, save.icon);
    if (!SaveMarker(save))
    {
        return std::nullopt;
    }
    for (const std::string &user : storage.List(usersDirectory))
    {
        std::string marker;
        const std::string path =
            SavedataDirectory(user) + "/" + profileMarkerSave + "/" + kMarkerFile;
        if (ReadWholeFile(path, marker) && marker == save.marker)
        {
            debugPrintf("FindLoggedInUser: user %s\n", user.c_str());
            return user;
        }
    }
    debugPrintf("FindLoggedInUser: the marker is in no user's savedata\n");
    return std::nullopt;
}

std::string MakeProfileDirectory(LocalStorage &storage, const std::string &user)
{
    const std::string directory = std::string(kProfilesDirectory) + "/" + user;
    storage.MakeDirectory(kProfilesDirectory);
    storage.MakeDirectory(directory);
    return directory;
}
