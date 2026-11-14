// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/ParamPfd.h"
#include "sync/ParamSfo.h"
#include "sync/Tar.h"

#include <string>
#include <vector>

/** Whom a save is to belong to once it comes down: this console and its logged-in user. */
struct SaveTarget
{
    SaveOwner owner;
    /** The console's IDPS, kIdpsSize bytes. */
    std::string idps;
};

/** What making a save the target's came to. */
enum class ResignResult
{
    /** The save is the target's already, or has no PARAM.SFO to tell whose it is. */
    Unchanged,
    Resigned,
    /** The save could not be made the target's: the game's key is not known, or it is damaged. */
    Refused
};

/**
 * Makes a save the target's: its PARAM.SFO gets the target's user and console, and its PARAM.PFD
 * is signed anew for them. Both change together or neither does.
 * @param files the save's files; PARAM.SFO and PARAM.PFD change in place
 * @param target whom the save is to belong to
 * @return what came of it
 */
ResignResult ResignSave(std::vector<TarFile> &files, const SaveTarget &target);
