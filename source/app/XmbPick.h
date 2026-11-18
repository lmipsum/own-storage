// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Notice.h"
#include "sync/SaveBrowser.h"

#include <string>

/** What the XMB asks of a save in the cloud: its row's X, or a choice of its options menu. */
enum class XmbSaveAction
{
    Download,
    Information,
    Delete,
};

/**
 * Reads a save's action as the action file holds it: a row's own "save:" choice, which downloads,
 * or one of the options menu's.
 * @param action the action file's content
 * @param what receives what is asked
 * @param name receives the save folder's name
 * @return false when the action is not one of a save
 */
bool ReadXmbSaveAction(const std::string &action, XmbSaveAction &what, std::string &name);

/**
 * Downloads a save as the Saved Data Utility does on X: one the console does not have without a
 * question, one it has otherwise after the browser asks to replace it, and one it has already is
 * only said to be there.
 * @param browser the saves in the cloud, listed already
 * @param name the save folder's name
 * @param changed receives whether the console's saves changed, which the XMB's lists then show;
 *        may be null
 * @return what came of it, as the user is told it
 */
Notice DownloadXmbSave(SaveBrowser &browser, const std::string &name, bool *changed = nullptr);

/**
 * What a save says of itself, as the game's load screen shows it: the game, the save's subtitle
 * and its detail, without downloading it.
 * @param browser the saves in the cloud, listed already
 * @param name the save folder's name
 * @return the text for a dialog; empty when the save is no longer in the cloud
 */
std::string XmbSaveInformation(SaveBrowser &browser, const std::string &name);

/**
 * Moves a save to the Drive trash, as the user confirmed on the XMB; one the console has too is
 * kept, as the next sync would upload it again.
 * @param browser the saves in the cloud, listed already
 * @param name the save folder's name
 * @param changed receives whether the save left the cloud, which the XMB's lists then show; may
 *        be null
 * @return what came of it, as the user is told it
 */
Notice DeleteXmbSave(SaveBrowser &browser, const std::string &name, bool *changed = nullptr);

/** What the user is told of a save the XMB still lists but the cloud no longer has. */
Notice XmbSaveGone(const std::string &name);

/** What the XMB asks of a game's saves in the cloud, as its options menu leaves it. */
enum class XmbGameAction
{
    DownloadAll,
    DeleteAll,
};

/**
 * Reads a game's action as the action file holds it.
 * @param action the action file's content
 * @param what receives what is asked
 * @param titleId receives the game's ID
 * @return false when the action is not one of a game
 */
bool ReadXmbGameAction(const std::string &action, XmbGameAction &what, std::string &titleId);

/**
 * Downloads every save of a game in the cloud as DownloadXmbSave() does each: only a different
 * copy on the console is asked about.
 * @param browser the saves in the cloud, listed already
 * @param titleId the game's ID
 * @param changed receives whether the console's saves changed, which the XMB's lists then show;
 *        may be null
 * @return how many of the game's saves the console has now
 */
Notice DownloadXmbGame(SaveBrowser &browser, const std::string &titleId, bool *changed = nullptr);

/**
 * Moves every save of a game in the cloud to the Drive trash, as the user confirmed on the XMB;
 * those the console has too are kept, as the next sync would upload them again.
 * @param browser the saves in the cloud, listed already
 * @param titleId the game's ID
 * @param changed receives whether a save left the cloud, which the XMB's lists then show; may be
 *        null
 * @return how many saves went, and how many were kept
 */
Notice DeleteXmbGame(SaveBrowser &browser, const std::string &titleId, bool *changed = nullptr);
