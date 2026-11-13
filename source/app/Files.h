// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * Reads a whole file.
 * @param path the file
 * @param text receives the content
 * @return false when the file cannot be read
 */
bool ReadWholeFile(const std::string &path, std::string &text);
