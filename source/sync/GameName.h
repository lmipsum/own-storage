// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * The ID of the game a save belongs to, which every save folder's name starts with.
 * @param save the save folder's name
 * @return the ID, such as "BCES00078"
 */
std::string TitleId(const std::string &save);

/**
 * A game's title as the user sees it, cleaned as GameLabel() cleans it; its ID when the title is
 * not known.
 * @param titleId the game's ID
 * @param title the game's title; may be empty
 */
std::string GameName(const std::string &titleId, const std::string &title);

/**
 * How the user sees a game named: its title and ID, or its ID alone when the title is not known.
 * A title from the cloud may be anything, so it is cut to the length a PARAM.SFO allows, where a
 * UTF-8 character starts, and loses its control characters and the spaces around it.
 * @param titleId the game's ID
 * @param title the game's title; may be empty
 * @return such as "Heavenly Sword (BCES00078)"
 */
std::string GameLabel(const std::string &titleId, const std::string &title);
