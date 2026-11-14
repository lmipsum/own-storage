// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/ParamSfo.h"

#include <cstdint>
#include <string>

/** A save owner for tests: a user number, a PSID of one repeated byte, and an account id. */
SaveOwner TestOwner(std::uint32_t userId, char psidByte, const std::string &accountId);

/** A PARAM.SFO with a title and an owner, laid out as the console lays it out. */
std::string MakeSfo(const std::string &title, const SaveOwner &owner);

/** A PARAM.SFO with a title, owned by user 1 of a console. */
std::string MakeSfo(const std::string &title);

/** The disc hash key of games started from the hard disk, which most saves are signed with. */
std::string FallbackDiscHashKey();

/**
 * A PARAM.PFD that signs a PARAM.SFO and a data file, laid out and signed as psdevwiki.com
 * describes. It is made apart from the code under test, so that each checks the other.
 * @param sfo the PARAM.SFO it signs
 * @param idps the console's IDPS
 * @param discHashKey the game's disc hash key
 * @param version 3, or 4, which derives its signing key with the keygen key
 */
std::string MakePfd(const std::string &sfo, const std::string &idps, const std::string &discHashKey,
                    int version);
