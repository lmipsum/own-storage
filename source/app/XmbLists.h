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
    /** The file a row writes its action to before it starts the app. */
    std::string actionFile;
    /** The app's title ID, which webMAN starts it by. */
    std::string appId;
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

// The list files' names in XmbPlace::directory. Each level of folders is a file of its own, as
// the XMB opens a folder only from a file other than the one that lists it.
const char xmbFolderFile[] = "owns_folder.xml";
const char xmbMenuFile[] = "owns_menu.xml";
const char xmbSavesFile[] = "owns_saves.xml";

/** The list files, one level of folders each. */
struct XmbLists
{
    /** The Own Storage folder, which the Game column lists. */
    std::string folder;
    /** Its rows, and the folder of the saves in the cloud. */
    std::string menu;
    /** The saves in the cloud. */
    std::string saves;
};

// What a row asks the app to do, as the action file holds it; a save's row adds its name.
const char xmbSync[] = "sync";
const char xmbUsbImport[] = "usb";
const char xmbSignIn[] = "signin";
const char xmbSignOut[] = "signout";
const char xmbSave[] = "save:";

/**
 * The XMB's lists. Each row writes its action to the action file and starts the app through
 * webMAN, which the XMB cannot do by itself.
 * @param place where the lists are, and how the app is started
 * @param signedIn whether the user is signed in; without it only the sign-in row is listed
 * @param saves the saves in the cloud
 * @return the files' content
 */
XmbLists MakeXmbLists(const XmbPlace &place, bool signedIn, const std::vector<XmbSave> &saves);

/**
 * The line of category_game.xml that puts the Own Storage folder in the XMB's Game column.
 * @param place where the lists are
 * @return the line, without its indentation
 */
std::string XmbCategoryEntry(const XmbPlace &place);

/**
 * Puts the Own Storage folder in category_game.xml, after the Saved Data Utility for PS3 saves in
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
