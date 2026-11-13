// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
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
    /** Whether a file or directory exists at path. */
    virtual bool Exists(const std::string &path) const = 0;
    /**
     * When a file was last changed.
     * @param path the file
     * @param time receives the time
     * @return false when the file cannot be read
     */
    virtual bool ModifiedTime(const std::string &path, std::time_t &time) const = 0;
    /**
     * A file's MD5, the way Drive reports md5Checksum.
     * @param path the file
     * @return the digest as lowercase hex; empty when the file cannot be read
     */
    virtual std::string Md5(const std::string &path) const = 0;
    /** Creates a directory; one that exists already stays as it is. */
    virtual void MakeDirectory(const std::string &path) = 0;
};
