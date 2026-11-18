// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * Where the console is told of the save folders the app wrote. The system's saved data list
 * comes from its own database, not from the folders, so a save written as files shows in the
 * Saved Data Utility only once the system registers its folder.
 */
class SaveRegistry
{
  public:
    virtual ~SaveRegistry() = default;

    /**
     * Asks for a save folder to be registered; it is called once every file of the save is written.
     * @param folder the save folder's full path
     */
    virtual void Register(const std::string &folder) = 0;
};

/** A registry that registers nothing, for saves the app only reads, such as a USB drive's. */
class NoSaveRegistry : public SaveRegistry
{
  public:
    void Register(const std::string &) override {}
};
