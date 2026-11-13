// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
#include <string>
#include <vector>

/** What the sync needs to know of a file to tell whether it changed. */
struct FileStatus
{
    unsigned long long size;
    std::time_t modified;
};

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
    /**
     * A file's size and when it last changed.
     * @param path the file
     * @param status receives them
     * @return false when there is no such file
     */
    virtual bool Status(const std::string &path, FileStatus &status) const = 0;
    /**
     * A file's MD5, the way Drive reports md5Checksum.
     * @param path the file
     * @return the digest as lowercase hex; empty when the file cannot be read
     */
    virtual std::string Md5(const std::string &path) const = 0;
    /**
     * Reads a whole file.
     * @param path the file
     * @param content receives its content
     * @return false when it cannot be read
     */
    virtual bool Read(const std::string &path, std::string &content) const = 0;
    /**
     * Writes a whole file, replacing one that is there.
     * @param path the file
     * @param content its content
     * @return false when it cannot be written
     */
    virtual bool Write(const std::string &path, const std::string &content) = 0;
    /** Deletes a file; one that is not there is fine. */
    virtual void Remove(const std::string &path) = 0;
    /** Creates a directory; one that exists already stays as it is. */
    virtual void MakeDirectory(const std::string &path) = 0;
};
