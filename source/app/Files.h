// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"

#include <string>

/**
 * Reads a whole file.
 * @param path the file
 * @param text receives the content
 * @return false when the file cannot be read
 */
bool ReadWholeFile(const std::string &path, std::string &text);

bool saveStateFile(const Json &content, const std::string &path);
Json loadWithBackup(const std::string &path, const std::string &backupPath);
