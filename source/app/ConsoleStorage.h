// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

/** The PS3 hard disk, through the lv2 file system calls. */
class ConsoleStorage : public LocalStorage
{
  public:
    std::vector<std::string> List(const std::string &directory) const override;
    bool Status(const std::string &path, FileStatus &status) const override;
    std::vector<DirectoryEntry> ListWithStatus(const std::string &directory) const override;
    std::string Md5(const std::string &path) const override;
    bool Read(const std::string &path, std::string &content) const override;
    bool ReadPart(const std::string &path, unsigned long long offset, std::size_t size,
                  std::string &content) const override;
    bool Write(const std::string &path, const std::string &content) override;
    bool Append(const std::string &path, const std::string &content) override;
    void Remove(const std::string &path) override;
    void MakeDirectory(const std::string &path) override;
};
