// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json/Json.h"
#include "sync/LocalStorage.h"

/**
 * Finds what there is to back up on the console: the save folders of every profile with their
 * files, and the PS1/PS2 memory cards.
 */
class SaveScanner
{
  public:
    explicit SaveScanner(const LocalStorage &storage);

    /**
     * Lists the saves as remote.json entries that are still to be uploaded: the profile folders
     * first, then their save folders, then the files in those, then the memory cards.
     * @return the entries
     */
    Json::Array Scan() const;

  private:
    const LocalStorage &m_storage;
};
