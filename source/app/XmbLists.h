// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/CloudSaveText.h"
#include "sync/LocalStorage.h"
#include "sync/SaveBrowser.h"

#include <string>
#include <vector>

/** Where the XMB finds the app's lists, and how its rows start the app. */
struct XmbPlace
{
    /** The folder of the list files, ending in a slash. */
    std::string directory;
    /** The folder of the rows' webMAN scripts, ending in a slash. */
    std::string scriptDirectory;
    /** The file a row's script writes its action to before it starts the app. */
    std::string actionFile;
    /** The file a row's script writes to have the launcher plugin start the app. */
    std::string launchRequest;
};

/** A save as the XMB lists it, like the Saved Data Utility. */
struct XmbSave
{
    /** The save folder's name, which a row's action names. */
    std::string name;
    /** The game's title. */
    std::string title;
    /** The save's subtitle; below it the game's ID, when the save was stored, and its size. */
    std::string info;
    /** The save's own icon; empty when the console has none of it to show. */
    std::string icon;
    /** Whether the console has the cloud's copy. */
    bool downloaded;
};

/**
 * What a row of the XMB starts: a webMAN script that writes the row's action to the action file
 * and asks the launcher plugin to start the app. webMAN runs scripts in its user mode, where its
 * web commands write no files.
 */
struct XmbScript
{
    /** The row's key, which names the script. */
    std::string key;
    /** The action, as the action file holds it. */
    std::string action;
};

// How a row's script is named in XmbPlace::scriptDirectory, after the row's key.
const char xmbScriptSuffix[] = ".bat";

// The list files' names in XmbPlace::directory. Each level of folders is a file of its own, as
// the XMB opens a folder only from a file other than the one that lists it.
const char xmbFolderFile[] = "owns_folder.xml";
const char xmbMenuFile[] = "owns_menu.xml";
const char xmbSavesFile[] = "owns_saves.xml";

/** The list files, one level of folders each. */
struct XmbLists
{
    /** The app's folder, Cloud Save Utility, which the Game column lists. */
    std::string folder;
    /** Its rows, and the folder of the saves in the cloud. */
    std::string menu;
    /** The saves in the cloud. */
    std::string saves;
    /** Each row's script by its name in XmbPlace::scriptDirectory, with the action it leaves. */
    std::vector<XmbScript> scripts;
};

// What a row asks the app to do, as the action file holds it; a save's row adds its name.
const char xmbSync[] = "sync";
const char xmbUsbImport[] = "usb";
const char xmbSignIn[] = "signin";
const char xmbSignOut[] = "signout";
const char xmbSave[] = "save:";

/**
 * The XMB's lists. Each row starts a webMAN script, which leaves its action for the app and
 * asks the launcher plugin to start it; the XMB cannot start the app by itself. Without webMAN the
 * rows that act give way to a row that says so, and the saves are only listed, as is a save of a
 * name no console gives.
 * @param place where the lists are, and how the app is started
 * @param signedIn whether the user is signed in; without it only the sign-in row is listed
 * @param webman whether webMAN MOD is there to run the rows' scripts
 * @param storageUse how full the Google storage is, shown under the saves in the cloud; empty when
 *        not known
 * @param saves the saves in the cloud
 * @return the files' content
 */
XmbLists MakeXmbLists(const XmbPlace &place, bool signedIn, bool webman,
                      const std::string &storageUse, const std::vector<XmbSave> &saves);

/**
 * A row's webMAN script.
 * @param place where the action file and the launch request are
 * @param script the row's script
 * @return the script: it writes the row's action to the action file anew, so that the file is as
 *         old as the row's choice, then the launch request
 */
std::string XmbScriptText(const XmbPlace &place, const XmbScript &script);

/**
 * The line of category_game.xml that puts the app's folder in the XMB's Game column.
 * @param place where the lists are
 * @return the line, without its indentation
 */
std::string XmbCategoryEntry(const XmbPlace &place);

/**
 * Puts the app's folder in category_game.xml, after the Saved Data Utility for PS3 saves in
 * every view that lists it, or first in the Game column's own view when none does; the file's own
 * line ends are kept.
 * @param category the file's content; changed in place
 * @param entry the line XmbCategoryEntry() gives
 * @return false when the folder is in it already, or the file has no place for it
 */
bool AddXmbCategoryEntry(std::string &category, const std::string &entry);

/**
 * The saves in the cloud as the XMB lists them, game by game and each game's newest first. A save
 * the console has the cloud's copy of shows its own icon.
 * @param browser the saves in the cloud, listed already
 * @param storage the console's hard disk
 * @param savedata the user's savedata directory
 * @param style how the console shows dates
 * @return the saves
 */
std::vector<XmbSave> XmbSaves(SaveBrowser &browser, const LocalStorage &storage,
                              const std::string &savedata, const DateStyle &style);
