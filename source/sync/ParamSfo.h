// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/** A save's PARAM.SFO, which names the game the save belongs to and whose the save is. */
const char paramSfoName[] = "PARAM.SFO";
/** The bytes of a console's open PSID, and of a PSN account id. */
const std::size_t kPsidSize = 16;
const std::size_t kAccountIdSize = 16;

/** Whose a save is: a console only loads the saves of its own users. */
struct SaveOwner
{
    /** The user's number on the console, such as 1 for /dev_hdd0/home/00000001. */
    std::uint32_t userId;
    /** The console's open PSID, kPsidSize bytes. */
    std::string psid;
    /** The user's PSN account id, kAccountIdSize bytes; NUL bytes without a PSN account. */
    std::string accountId;
};

/** Whether two owners are the same user of the same console. */
bool operator==(const SaveOwner &a, const SaveOwner &b);

/**
 * The game title a PARAM.SFO holds, as the console shows it.
 * @param sfo the whole file
 * @return the title; empty when the file has none or is not a PARAM.SFO
 */
std::string ReadSfoTitle(const std::string &sfo);

/**
 * Whose a save is, as its PARAM.SFO says.
 * @param sfo the whole file
 * @param owner receives the owner
 * @return false when the file is not a PARAM.SFO of a save
 */
bool ReadSfoOwner(const std::string &sfo, SaveOwner &owner);

/**
 * Makes a save another user's, or another console's, in its PARAM.SFO. Only the owner changes;
 * the file keeps its size and every other byte.
 * @param sfo the whole file; changed in place
 * @param owner the new owner
 * @return false when the file is not a PARAM.SFO of a save, or the owner is not complete
 */
bool WriteSfoOwner(std::string &sfo, const SaveOwner &owner);
