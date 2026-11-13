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

/**
 * Writes a state file and makes it readable to all.
 * @param content the state to write
 * @param path the file to write it to
 * @return false when the file could not be written
 */
bool SaveStateFile(const Json &content, const std::string &path);

/**
 * Reads a state file, or its backup when the file is missing or cannot be parsed, and then
 * writes the backup's content over the spoiled file.
 * @param path the state file
 * @param backupPath the copy that every save writes next to it
 * @return the content; empty when neither can be read
 */
Json LoadWithBackup(const std::string &path, const std::string &backupPath);
