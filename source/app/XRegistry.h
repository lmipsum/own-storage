// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

/** The console's registry of settings, which also holds every user's PSN sign-in. */
const char xRegistryPath[] = "/dev_flash2/etc/xRegistry.sys";

/**
 * A user's PSN account id, as the registry holds it. The registry also holds PSN passwords, so
 * nothing but this one value is ever taken from it.
 * @param registry the whole xRegistry.sys
 * @param user the user's folder name under /dev_hdd0/home, such as "00000001"
 * @return the account id, kAccountIdSize bytes; NUL bytes when the user has no PSN account;
 * nothing when the user has one but its id cannot be found, which must not pass for none
 */
std::optional<std::string> FindAccountId(const std::string &registry, const std::string &user);
