// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

/** The file that signs a save, so that a console loads only its own users' saves. */
const char paramPfdName[] = "PARAM.PFD";
/** The bytes of a console's IDPS. */
const std::size_t kIdpsSize = 16;

/** What re-signing a PARAM.PFD came to. */
enum class PfdResign
{
    Resigned,
    /** The PFD does not sign PARAM.SFO, as in old saves, so it needs no change. */
    NoSfoEntry,
    /** Not a PARAM.PFD, or its signatures do not hold. */
    Damaged
};

/**
 * Re-signs a save's PARAM.PFD after its PARAM.SFO was made another console's or user's. The PFD
 * is first checked against the PARAM.SFO it signed, which tells whether the keys are right; it
 * is changed only when they are. A game may sign with a disc hash key of its own, which is not
 * known; the disc hash is made anew with the key of games started from the hard disk, and a
 * console loaded a disc game's save signed so. Written from the format psdevwiki.com describes
 * (PARAM.PFD, Keys), with the keys public implementations use.
 * @param pfd the whole PARAM.PFD; changed in place only when re-signed
 * @param signedSfo the PARAM.SFO as the PFD signed it
 * @param newSfo the PARAM.SFO with its new owner
 * @param idps the IDPS of the console the save is to load on, kIdpsSize bytes
 * @return what came of it
 */
PfdResign ResignPfd(std::string &pfd, const std::string &signedSfo, const std::string &newSfo,
                    const std::string &idps);
