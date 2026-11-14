// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/Resign.h"

#include <optional>
#include <string>

/**
 * Whom the saves brought down for a user of this console are to belong to.
 * @param user the user's folder name under /dev_hdd0/home, such as "00000001"
 * @return the user and the console; nothing when the console's ids, which need a custom firmware,
 * or the user's PSN account id cannot be read
 */
std::optional<SaveTarget> ReadSaveTarget(const std::string &user);
