// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/** A save's PARAM.SFO, which names the game the save belongs to. */
const char paramSfoName[] = "PARAM.SFO";

/**
 * The game title a PARAM.SFO holds, as the console shows it.
 * @param sfo the whole file
 * @return the title; empty when the file has none or is not a PARAM.SFO
 */
std::string ReadSfoTitle(const std::string &sfo);
