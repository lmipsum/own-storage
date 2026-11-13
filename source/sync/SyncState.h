// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"

#include <ppu-types.h>

#include <string>

extern const std::string remoteResourceFile;
extern const std::string remoteBackup;
extern const std::string remoteJournal;
extern const std::string creatingStatus;
extern Json remoteResourceRoot;

bool isPendingCreation(const Json &resource);
void writeChangesToFile();
void recordEntry(u32 index);
void replayJournal();
void markCreating(u32 index);
void recordFolder(u32 index, const std::string &parentId, const Json &folder);
void recordFile(u32 index, const std::string &parentId, const Json &file);
