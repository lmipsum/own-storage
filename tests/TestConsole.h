// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

#include <ctime>
#include <string>

/**
 * A console hard disk in a fresh temporary directory: every /dev_hdd0 path the sync uses lands
 * under it, other paths stay as they are.
 */
class TestConsole : public LocalStorage
{
  public:
    TestConsole();
    ~TestConsole() override;
    TestConsole(const TestConsole &) = delete;
    TestConsole &operator=(const TestConsole &) = delete;

    /** Where a console path is on the PC. */
    std::string Map(const std::string &path) const;
    /** Writes a file, with its directories, and sets when it was last changed. */
    void Write(const std::string &path, const std::string &content, std::time_t changed);
    /** A file's content; empty when it cannot be read. */
    std::string Read(const std::string &path) const;
    /** A directory on the PC for files outside the console, such as remote.json. */
    const std::string &Scratch() const;

    std::vector<std::string> List(const std::string &directory) const override;
    bool Exists(const std::string &path) const override;
    bool ModifiedTime(const std::string &path, std::time_t &time) const override;
    std::string Md5(const std::string &path) const override;
    void MakeDirectory(const std::string &path) override;

  private:
    std::string m_root;
    std::string m_scratch;
};
