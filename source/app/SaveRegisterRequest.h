// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"
#include "sync/SaveRegistry.h"

#include <string>

/**
 * Registers save folders through the launcher plugin: the app cannot reach the XMB's command that
 * registers a folder, so it leaves each one in the plugin's request file, a full path a line, and
 * the plugin registers them once the user is back on the XMB
 * (plugin/launcher/source/save_register.c).
 */
class SaveRegisterRequest : public SaveRegistry
{
  public:
    /**
     * @param storage the console's hard disk
     * @param requestFile the file the launcher plugin takes the folders from
     */
    SaveRegisterRequest(LocalStorage &storage, const std::string &requestFile);

    void Register(const std::string &folder) override;

  private:
    LocalStorage &m_storage;
    const std::string m_requestFile;
};
