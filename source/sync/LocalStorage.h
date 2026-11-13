// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

/**
 * The console's hard disk as the sync sees it, so that the sync does not depend on the console's
 * file system calls.
 */
class LocalStorage
{
  public:
    virtual ~LocalStorage() = default;

    /**
     * The names in a directory.
     * @param directory the directory
     * @return the names, without "." and ".."; empty when the directory cannot be read
     */
    virtual std::vector<std::string> List(const std::string &directory) const = 0;
};
