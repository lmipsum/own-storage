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
    /** The save's subtitle; its folder's name when it has none. */
    std::string subtitle;
    /** The row's description: the subtitle, below it when the save was stored, and its size. */
    std::string info;
    /** When the save was stored, as the console shows dates. */
    std::string updated;
    /** The save's size, as the console shows it. */
    std::string size;
    /** What the game says of the save, without the line breaks it may end with. */
    std::string detail;
    /** When the save was stored, as Drive gives it, which sorts as text does. */
    std::string modified;
    /** The game's ID. */
    std::string titleId;
    /** The save's own icon; empty when the console has none of it to show. */
    std::string icon;
    /** Whether the console has the cloud's copy. */
    bool downloaded;
};

/** A game as the XMB lists it: a folder of its saves. */
struct XmbGame
{
    /** The game's title. */
    std::string title;
    /** The game's ID. */
    std::string titleId;
    /** Its saves, the one stored last first. */
    std::vector<XmbSave> saves;
};

/** The order the XMB lists the saves in the cloud in. */
enum class XmbSort
{
    DateNewest,
    DateOldest,
    /** By the game's title, then the save's subtitle. */
    Title,
    /** By the save's subtitle, then the game's title. */
    Subtitle,
};

/** Whether the XMB lists the saves in the cloud in a folder per game, or all in one list. */
enum class XmbGroup
{
    ByTitle,
    All,
};

/** How the XMB lists the saves in the cloud, as the user chose it on the XMB. */
struct XmbView
{
    XmbSort sort;
    XmbGroup group;
};

/**
 * Reads the view the launcher plugin keeps, in the app's folder as xmbViewFile: two letters, the
 * sort's (d newest first, o oldest first, t title, s subtitle) and the group's (t by title, a all).
 * @param text the file's content; a line end after the letters is allowed
 * @return the view; newest first by title when the text is no view, as before the user chose one
 */
XmbView ParseXmbView(const std::string &text);

/**
 * The list file of the menu, which opens the saves in the cloud in a view, named as long as
 * xmbMenuFile: the launcher plugin switches views by changing the name in the XMB's stored address,
 * and the XMB keeps each file it read by its name, so each view needs a file of its own.
 * @return such as "owns_m_dt.xml"
 */
std::string XmbMenuVariant(const XmbView &view);

/**
 * The list file of the saves in the cloud in a view, as long as xmbSavesFile.
 * @return such as "owns_sa_dt.xml"
 */
std::string XmbSavesVariant(const XmbView &view);

/**
 * The list file of each game's saves in an order, as long as xmbGamesFile: only the saves in a
 * folder per game need it, so its group letter is always that one's, such as "owns_ga_dt.xml".
 */
std::string XmbGamesVariant(XmbSort sort);

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

/** A file of the lists in XmbPlace::directory. */
struct XmbFile
{
    std::string name;
    std::string content;
};

// How a row's script is named in XmbPlace::scriptDirectory, after the row's key.
const char xmbScriptSuffix[] = ".bat";

// The list files' names in XmbPlace::directory. Each level of folders is a file of its own, as
// the XMB opens a folder only from a file other than the one that lists it. These hold the view
// the user chose last, which the XMB opens after the console starts.
const char xmbFolderFile[] = "owns_folder.xml";
const char xmbMenuFile[] = "owns_menu.xml";
const char xmbSavesFile[] = "owns_saves.xml";
const char xmbGamesFile[] = "owns_games.xml";

/**
 * The rows' index in XmbPlace::directory, from which the launcher plugin tells what a focused row
 * is by its title and description, and shows what the row has no room for. A line per row, its
 * fields separated by tabs; a backslash, a tab and a line break in a field are written as "\\",
 * "\t" and "\n". A game's folder comes before its saves:
 *   G, key, game ID, title, description, number of saves
 *   S, key, save folder's name, title, description, subtitle, stored, size, detail, game ID
 * The title and the description are the row's own, so they match what the XMB shows.
 */
const char xmbRowsFile[] = "owns_rows.txt";

// The view the launcher plugin keeps in the app's folder, which ParseXmbView() reads.
const char xmbViewFile[] = "xmb_view.txt";

/** The list files and the rows' scripts. */
struct XmbLists
{
    /** Every file of XmbPlace::directory: the lists of every view, and the rows' index. */
    std::vector<XmbFile> files;
    /** Each row's script by its name in XmbPlace::scriptDirectory, with the action it leaves. */
    std::vector<XmbScript> scripts;
};

// What a row asks the app to do, as the action file holds it; a save's row adds its name.
const char xmbSync[] = "sync";
const char xmbUsbImport[] = "usb";
const char xmbSignIn[] = "signin";
const char xmbSignOut[] = "signout";
const char xmbSave[] = "save:";
// What a save's options menu asks for, as the launcher plugin writes it; the save's name follows.
// A delete comes once the user confirmed it there.
const char xmbInformation[] = "info:";
const char xmbDelete[] = "delete:";
// What a game's options menu asks for, as the launcher plugin writes it; the game's ID follows.
// A delete comes once the user confirmed it there.
const char xmbDownloadAll[] = "downloadall:";
const char xmbDeleteAll[] = "deleteall:";

/**
 * The XMB's lists in every view. Each row starts a webMAN script, which leaves its action for the
 * app and asks the launcher plugin to start it; the XMB cannot start the app by itself. Without
 * webMAN the rows that act give way to a row that says so, and the saves are only listed, as is a
 * save of a name no console gives. The saves in the cloud come before the rows that act. A save's
 * row has the same key and script in every view, so a view can change without the app.
 * @param place where the lists are, and how the app is started
 * @param signedIn whether the user is signed in; without it only the sign-in row is listed
 * @param webman whether webMAN MOD is there to run the rows' scripts
 * @param storageUse how full the Google storage is, shown under the saves in the cloud; empty when
 *        not known
 * @param games the games in the cloud by title, with their saves newest first
 * @param chosen the view the user chose last, which the files of the lists' own names hold
 * @return the files' content
 */
XmbLists MakeXmbLists(const XmbPlace &place, bool signedIn, bool webman,
                      const std::string &storageUse, const std::vector<XmbGame> &games,
                      const XmbView &chosen);

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
 * The games in the cloud as the XMB lists them, with each game's saves, newest first. A save the
 * console has the cloud's copy of shows its own icon.
 * @param browser the saves in the cloud, listed already
 * @param storage the console's hard disk
 * @param savedata the user's savedata directory
 * @param style how the console shows dates
 * @return the games
 */
std::vector<XmbGame> XmbGames(SaveBrowser &browser, const LocalStorage &storage,
                              const std::string &savedata, const DateStyle &style);
