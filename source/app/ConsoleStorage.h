// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

/** The PS3 hard disk, through the lv2 file system calls. */
class ConsoleStorage : public LocalStorage
{
  public:
    std::vector<std::string> List(const std::string &directory) const override;
};
