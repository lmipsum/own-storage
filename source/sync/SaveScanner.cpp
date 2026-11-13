// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/SaveScanner.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "drive/GoogleDrive.h"
#include "sync/SyncState.h"

#include <sstream>
#include <string.h>
#include <vector>

#include <sys/file.h>

bool pathExists(std::string pathToCheck)
{
    sysFSStat entry;
    return sysFsStat(pathToCheck.c_str(), &entry) == 0;
}

/**
 * Builds a local resource tree given the path of a local profile folder
 */
void buildLocalResourceTree()
{
    std::vector<Json> resourceTree;

    debugPrintf("Building Resource Tree....\n");

    sysFSDirent entry;
    s32 fd;
    u64 read;
    std::vector<std::string> profileList;
    std::vector<std::string> profilePaths;

    std::string entryName = "";
    std::string path = "/dev_hdd0/home";

    // Check how many profiles exists in user's HDD 0000001 etc
    if (sysFsOpendir(path.c_str(), &fd) == 0)
    {
        while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
        {
            if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
            {
                continue;
            }
            else
            {
                entryName.clear();
                entryName.assign(entry.d_name, strlen(entry.d_name));

                profileList.push_back(entryName);
                profilePaths.push_back(path + "/" + entryName + "/savedata");

                Json resource;
                resource.Add("id", Json("nill"));
                resource.Add("title", Json(entryName));
                resource.Add("path", Json(path + "/" + entryName + "/savedata"));
                resource.Add("mimeType", Json(folderMime));
                resource.Add("parentid", Json(config.rootFolderId));
                resource.Add("parentFolder", Json(driveFolderName));
                resource.Add("modifiedDate", Json("0"));
                resource.Add("status", Json(uploadStatus));

                resourceTree.push_back(Json(resource));
            }
        }
        sysFsClosedir(fd);
        fd = -1;
    }

    std::vector<std::string>::const_iterator k = profileList.begin();
    std::vector<std::string>::const_iterator l = profilePaths.begin();
    std::vector<std::string> folderList;
    std::vector<std::string> pathList;

    // Recursively list all saves under every profile
    while (k != profileList.end())
    {
        debugPrintf("Entering Save Path...%s\n", l->c_str());

        if (sysFsOpendir(l->c_str(), &fd) == 0)
        {
            while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
            {
                if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                {
                    continue;
                }
                else
                {
                    folderList.push_back(entry.d_name);

                    entryName.clear();
                    entryName.assign(entry.d_name, strlen(entry.d_name));

                    std::stringstream fPath;
                    fPath << *l << "/" << entryName;

                    pathList.push_back(fPath.str());

                    // debugPrintf("Save Path Entry...%s\n",entryName.c_str());

                    Json resource;
                    resource.Add("id", Json("nill"));
                    resource.Add("title", Json(entryName));
                    resource.Add("path", Json(fPath.str()));
                    resource.Add("mimeType", Json(folderMime));
                    resource.Add("parentid", Json("nill"));
                    resource.Add("parentFolder", Json(k->c_str()));
                    resource.Add("modifiedDate", Json("0"));
                    resource.Add("status", Json(uploadStatus));

                    resourceTree.push_back(Json(resource));
                }
            }
            sysFsClosedir(fd);
            fd = -1;
        }
        ++k, ++l;
    }

    debugPrintf("Directory enumeration complete...listing files inside them...\n");

    std::vector<std::string>::const_iterator i = pathList.begin();
    std::vector<std::string>::const_iterator j = folderList.begin();

    // Recursively list all files under every save directory
    while (i != pathList.end())
    {
        if (sysFsOpendir(i->c_str(), &fd) == 0)
        {
            while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
            {
                if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                {
                    continue;
                }
                else
                {
                    entryName.clear();
                    entryName.assign(entry.d_name, strlen(entry.d_name));

                    std::stringstream fPath;
                    fPath << *i << "/" << entryName;

                    // debugPrintf("Entry Path...%s\n",fPath.str().c_str());

                    Json resource;
                    resource.Add("id", Json("nill"));
                    resource.Add("title", Json(entryName));
                    resource.Add("path", Json(fPath.str()));
                    resource.Add("mimeType", Json(octetMime));
                    resource.Add("parentFolder", Json(j->c_str()));
                    resource.Add("parentid", Json("nill"));
                    resource.Add("modifiedDate", Json("0"));
                    resource.Add("fileSize", Json("0"));
                    resource.Add("md5Checksum", Json("0"));
                    resource.Add("status", Json(uploadStatus));

                    resourceTree.push_back(Json(resource));
                }
            }
            sysFsClosedir(fd);
            fd = -1;
        }
        ++i;
        ++j;
    }

    // Check for Existence of PS1 and PS2 memory cards
    if (pathExists("/dev_hdd0/savedata"))
    {
        if (pathExists("/dev_hdd0/savedata/vmc"))
        {
            if (sysFsOpendir("/dev_hdd0/savedata/vmc", &fd) == 0)
            {
                while (!sysFsReaddir(fd, &entry, &read) && strlen(entry.d_name) > 0)
                {
                    if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
                    {
                        continue;
                    }
                    else
                    {
                        entryName.clear();
                        entryName.assign(entry.d_name, strlen(entry.d_name));

                        debugPrintf("Memcard%s\n", entryName.c_str());

                        Json resource;
                        resource.Add("id", Json("nill"));
                        resource.Add("title", Json(entryName));
                        resource.Add("path", Json("/dev_hdd0/savedata/vmc/" + entryName));
                        resource.Add("mimeType", Json(octetMime));
                        resource.Add("parentid", Json(config.rootFolderId));
                        resource.Add("parentFolder", Json(driveFolderName));
                        resource.Add("modifiedDate", Json("0"));
                        resource.Add("status", Json(uploadStatus));

                        resourceTree.push_back(Json(resource));
                    }
                }
                sysFsClosedir(fd);
                fd = -1;
            }
        }
    }

    debugPrintf("Resource Tree built, writing to Resource Json\n");

    // Overwrite local.json every time
    localResourceRoot.Add("data", Json(resourceTree));
    SaveStateFile(localResourceRoot, localResourceFile);

    debugPrintf("Resource file has been written successfully...\n");
}
