// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbLists.h"

#include "sync/GameName.h"

#include <algorithm>
#include <sstream>

namespace
{
// The view of a list file that the XMB opens.
const char kFolderView[] = "owns_folder_view";
const char kMenuView[] = "owns_menu_view";
const char kSavesView[] = "owns_saves_view";
const char kCloudKey[] = "owns_cloud";
const char kGameKeyPrefix[] = "owns_game_";
const char kGameViewSuffix[] = "_view";
const char kSaveKeyPrefix[] = "owns_save_";
// The key of the app's folder in category_game.xml, which tells it is there already.
const char kCategoryKey[] = "owns_root";
// The Saved Data Utility for PS3 saves in category_game.xml; the folder goes after it.
const char kSavedDataUtilityKey[] = "key=\"seg_sdps3\"";
// The folder is named like the system's Saved Data Utility, beside which it is.
const char kFolderTitle[] = "Cloud Save Utility";
// The system's own texts, by their names in the Game column's resources, which the XMB shows in the
// system's language; only the USB row's description has none like it.
const char kFolderInfoRsc[] = "msg_online_storage_explanation";
const char kCloudSavesRsc[] = "msg_online_storage";
const char kSyncRsc[] = "msg_sync_with_server";
const char kUsbRsc[] = "msg_usb";
const char kSignInRsc[] = "msg_signin";
const char kSignOutRsc[] = "msg_signout";
const char kUsbInfo[] = "Uploads the saves on a USB drive to your Google Drive.";
// The system's icons, from those the Game column can show: the rows' own; a downloaded save
// without an icon of its own, which the folders of the saves in the cloud and of each game share;
// and a save only in the cloud, which the Cloud Save Utility folder and the row that asks for
// webMAN share.
const char kSyncIcon[] = "tex_loading_icon";
const char kUsbIcon[] = "item_tex_usb_connection";
const char kAccountIcon[] = "tex_Avatar_Default";
const char kDownloadedIcon[] = "item_tex_ps3util";
const char kInCloudIcon[] = "tex_album_icon";
// The icon file of a save folder.
const char kSaveIcon[] = "ICON0.PNG";
// The longest name the console gives a save folder.
const std::size_t kSaveNameMaxBytes = 31;

// Every view; a view's letters, as the launcher plugin writes them, are at its sort's and group's
// place in these.
const XmbSort kSorts[] = {XmbSort::DateNewest, XmbSort::DateOldest, XmbSort::Title,
                          XmbSort::Subtitle};
const XmbGroup kGroups[] = {XmbGroup::ByTitle, XmbGroup::All};
const char kSortLetters[] = "dots";
const char kGroupLetters[] = "ta";
const XmbView kDefaultView{XmbSort::DateNewest, XmbGroup::ByTitle};
// The variants' names: a stem, the view's letters and the suffix make them as long as the files of
// the lists' own names.
const char kMenuVariantStem[] = "owns_m_";
const char kSavesVariantStem[] = "owns_sa_";
const char kGamesVariantStem[] = "owns_ga_";
const char kListSuffix[] = ".xml";
// The rows' index: what tells a game's row from a save's, and what separates the fields.
const char kGameRecord[] = "G";
const char kSaveRecord[] = "S";
const char kFieldSeparator = '\t';

/** A count in digits; the console's C++ library offers no std::to_string. */
std::string Digits(std::size_t count)
{
    std::ostringstream text;
    text << count;
    return text.str();
}

char SortLetter(XmbSort sort)
{
    return kSortLetters[static_cast<std::size_t>(sort)];
}

char GroupLetter(XmbGroup group)
{
    return kGroupLetters[static_cast<std::size_t>(group)];
}

/** A variant's name: its stem, the view's letters and the suffix. */
std::string VariantName(const char *stem, XmbSort sort, XmbGroup group)
{
    return stem + std::string(1, SortLetter(sort)) + GroupLetter(group) + kListSuffix;
}

/**
 * Whether a name is one the console gives a save folder: capitals, digits, '-' and '_'. Only such
 * a name goes in a row's script, where any other could end its line and add a command.
 */
bool IsSaveFolderName(const std::string &name)
{
    if (name.empty() || name.size() > kSaveNameMaxBytes)
    {
        return false;
    }
    for (const char c : name)
    {
        const bool allowed =
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!allowed)
        {
            return false;
        }
    }
    return true;
}

/** A text as an XML attribute or element holds it. */
std::string Escaped(const std::string &text)
{
    std::string escaped;
    for (const char c : text)
    {
        switch (c)
        {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped += c;
        }
    }
    return escaped;
}

/** A game folder's description: its ID and how many saves it has. */
std::string GameInfo(const XmbGame &game)
{
    return game.titleId + " | " + SaveCount(game.saves.size());
}

/** A pair of an attribute table. */
std::string Pair(const char *key, const std::string &value)
{
    return std::string("\t\t\t\t<Pair key=\"") + key + "\"><String>" + Escaped(value) +
           "</String></Pair>\n";
}

/** An icon's pair: the file when there is one, the system's icon otherwise. */
std::string IconPair(const std::string &file, const char *systemIcon)
{
    return file.empty() ? Pair("icon_rsc", systemIcon) : Pair("icon", file);
}

/** Where the XMB reads a view of a list file. */
std::string ViewSource(const XmbPlace &place, const std::string &file, const std::string &view)
{
    return "xmb://localhost" + place.directory + file + "#" + view;
}

/** The pairs that make a row run its webMAN script, through webMAN's proxy. */
std::string ActionPairs(const XmbPlace &place, const std::string &key)
{
    return Pair("module_name", "idle_plugin") + Pair("bar_action", "none") +
           Pair("module_action", "/play.ps3" + place.scriptDirectory + key + xmbScriptSuffix);
}

/** A row's attribute table. */
std::string Table(const std::string &key, const std::string &pairs)
{
    return "\t\t\t<Table key=\"" + key + "\">\n" + pairs + "\t\t\t</Table>\n";
}

/** A row of the XMB's action class; it starts what its pairs name, nothing without them. */
std::string ActionItem(const std::string &key)
{
    return "\t\t\t<Item class=\"type:x-xmb/module-action\" key=\"" + key + "\" attr=\"" + key +
           "\"/>\n";
}

/** A row that opens a folder. */
std::string FolderItem(const std::string &key, const std::string &source)
{
    return "\t\t\t<Query class=\"type:x-xmb/folder-pixmap\" key=\"" + key + "\" attr=\"" + key +
           "\" src=\"" + Escaped(source) + "\"/>\n";
}

/** A view with its tables and rows. */
std::string View(const std::string &id, const std::string &tables, const std::string &items)
{
    return "\t<View id=\"" + id + "\">\n\t\t<Attributes>\n" + tables +
           "\t\t</Attributes>\n\t\t<Items>\n" + items + "\t\t</Items>\n\t</View>\n";
}

/** A list file of views. */
std::string Document(const std::string &views)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<XMBML version=\"1.0\">\n" + views +
           "</XMBML>\n";
}

/**
 * The menu's rows but the folder of the saves in the cloud, which opens another file in each view.
 * The folder comes after the row that asks for webMAN and before the rows that act.
 */
struct MenuRows
{
    std::string tables;
    std::string leadingItems;
    bool cloud;
    std::string actionItems;
};

/**
 * The menu's rows.
 * @param scripts receives the scripts of the rows that act
 */
MenuRows MakeMenuRows(const XmbPlace &place, bool signedIn, bool webman,
                      const std::string &storageUse, std::vector<XmbScript> &scripts)
{
    MenuRows rows{std::string(), std::string(), signedIn, std::string()};
    // A row's title is a system text; its description, when it has one, is the app's own.
    const auto addAction = [&](const char *key, const char *icon, const char *title,
                               const char *info, const char *action)
    {
        if (!webman)
        {
            return;
        }
        const std::string described = info != nullptr ? Pair("info", info) : std::string();
        rows.tables += Table(key, Pair("icon_rsc", icon) + Pair("title_rsc", title) + described +
                                      ActionPairs(place, key));
        rows.actionItems += ActionItem(key);
        scripts.push_back(XmbScript{key, action});
    };
    if (!webman)
    {
        rows.tables += Table("owns_webman",
                             Pair("icon_rsc", kInCloudIcon) + Pair("title", "Install webMAN MOD") +
                                 Pair("info", "The rows here need it to start Own Storage."));
        rows.leadingItems += ActionItem("owns_webman");
    }
    if (!signedIn)
    {
        addAction("owns_signin", kAccountIcon, kSignInRsc, nullptr, xmbSignIn);
        return rows;
    }
    const std::string used = storageUse.empty() ? std::string() : Pair("info", storageUse);
    rows.tables += Table(kCloudKey, Pair("icon_rsc", kDownloadedIcon) +
                                        Pair("title_rsc", kCloudSavesRsc) + used);
    addAction("owns_sync", kSyncIcon, kSyncRsc, nullptr, xmbSync);
    addAction("owns_usb", kUsbIcon, kUsbRsc, kUsbInfo, xmbUsbImport);
    addAction("owns_signout", kAccountIcon, kSignOutRsc, nullptr, xmbSignOut);
    return rows;
}

/** The menu's file, whose folder of the saves in the cloud opens a file of them. */
std::string MenuDocument(const XmbPlace &place, const MenuRows &rows, const std::string &savesFile)
{
    const std::string cloud = rows.cloud
                                  ? FolderItem(kCloudKey, ViewSource(place, savesFile, kSavesView))
                                  : std::string();
    return Document(View(kMenuView, rows.tables, rows.leadingItems + cloud + rows.actionItems));
}

/** A save's row, and the key it has in every view. */
struct SaveRow
{
    const XmbSave *save;
    std::string key;
};

/** A game's folder row, and its saves' rows, newest first. */
struct GameRow
{
    const XmbGame *game;
    std::string key;
    std::vector<SaveRow> saves;
};

/**
 * Keys every row once, the saves' running on from game to game as each names a script of the one
 * folder: in the order the games come, by title, and their saves, newest first.
 */
std::vector<GameRow> KeyRows(const std::vector<XmbGame> &games)
{
    std::vector<GameRow> rows;
    std::size_t saveNumber = 0;
    for (std::size_t g = 0; g < games.size(); ++g)
    {
        GameRow game{&games[g], kGameKeyPrefix + Digits(g), std::vector<SaveRow>()};
        for (const XmbSave &save : games[g].saves)
        {
            game.saves.push_back(SaveRow{&save, kSaveKeyPrefix + Digits(saveNumber++)});
        }
        rows.push_back(game);
    }
    return rows;
}

/**
 * Whether a save's row acts: with webMAN, for a save of a name a console gives; any other is only
 * listed.
 */
bool Acts(bool webman, const XmbSave &save)
{
    return webman && IsSaveFolderName(save.name);
}

/** Whether text a comes before text b, and on a tie text c before text d, from A to Z. */
bool LessByTwo(const std::string &a, const std::string &b, const std::string &c,
               const std::string &d)
{
    return LessIgnoringCase(a, b) || (!LessIgnoringCase(b, a) && LessIgnoringCase(c, d));
}

/** Whether save a comes before save b in a sort. */
bool Before(const XmbSave &a, const XmbSave &b, XmbSort sort)
{
    // RFC 3339 times in one form sort as text does.
    switch (sort)
    {
    case XmbSort::DateNewest:
        return a.modified > b.modified;
    case XmbSort::DateOldest:
        return a.modified < b.modified;
    case XmbSort::Title:
        return LessByTwo(a.title, b.title, a.subtitle, b.subtitle);
    case XmbSort::Subtitle:
        return LessByTwo(a.subtitle, b.subtitle, a.title, b.title);
    }
    return false;
}

/** Saves' rows in a sort; those that sort alike keep their order, as their keys go. */
std::vector<SaveRow> Sorted(std::vector<SaveRow> rows, XmbSort sort)
{
    std::stable_sort(rows.begin(), rows.end(), [sort](const SaveRow &a, const SaveRow &b)
                     { return Before(*a.save, *b.save, sort); });
    return rows;
}

/** A view of saves' rows. */
std::string SavesView(const XmbPlace &place, bool webman, const std::string &id,
                      const std::vector<SaveRow> &rows)
{
    std::string tables;
    std::string items;
    for (const SaveRow &row : rows)
    {
        const XmbSave &save = *row.save;
        const std::string actionPairs =
            Acts(webman, save) ? ActionPairs(place, row.key) : std::string();
        tables +=
            Table(row.key, IconPair(save.icon, save.downloaded ? kDownloadedIcon : kInCloudIcon) +
                               Pair("title", save.title) + Pair("info", save.info) + actionPairs);
        items += ActionItem(row.key);
    }
    return View(id, tables, items);
}

/** The file of each game's saves, a view each, in a sort. */
std::string GamesDocument(const XmbPlace &place, bool webman, const std::vector<GameRow> &games,
                          XmbSort sort)
{
    std::string views;
    for (const GameRow &game : games)
    {
        views += SavesView(place, webman, game.key + kGameViewSuffix, Sorted(game.saves, sort));
    }
    return Document(views);
}

/**
 * The file of the saves in the cloud in a view: a folder per game by title, which opens the game's
 * view in a file of each game's saves, or all saves in one list.
 */
std::string SavesDocument(const XmbPlace &place, bool webman, const std::vector<GameRow> &games,
                          const XmbView &view, const std::string &gamesFile)
{
    if (view.group == XmbGroup::All)
    {
        std::vector<SaveRow> all;
        for (const GameRow &game : games)
        {
            all.insert(all.end(), game.saves.begin(), game.saves.end());
        }
        return Document(SavesView(place, webman, kSavesView, Sorted(all, view.sort)));
    }
    std::string tables;
    std::string items;
    for (const GameRow &game : games)
    {
        tables +=
            Table(game.key, Pair("icon_rsc", kDownloadedIcon) + Pair("title", game.game->title) +
                                Pair("info", GameInfo(*game.game)));
        items += FolderItem(game.key, ViewSource(place, gamesFile, game.key + kGameViewSuffix));
    }
    return Document(View(kSavesView, tables, items));
}

/** A field of the rows' index, whose tabs and line breaks would end the field or the row. */
std::string IndexField(const std::string &text)
{
    std::string field;
    for (const char c : text)
    {
        switch (c)
        {
        case '\\':
            field += "\\\\";
            break;
        case '\t':
            field += "\\t";
            break;
        case '\n':
            field += "\\n";
            break;
        default:
            field += c;
        }
    }
    return field;
}

/** A row of the rows' index. */
std::string IndexLine(const std::vector<std::string> &fields)
{
    std::string line;
    for (std::size_t f = 0; f < fields.size(); ++f)
    {
        if (f > 0)
        {
            line += kFieldSeparator;
        }
        line += IndexField(fields[f]);
    }
    return line + "\n";
}

/** The rows' index, as xmbRowsFile tells it. */
std::string RowsIndex(const std::vector<GameRow> &games)
{
    std::string index;
    for (const GameRow &game : games)
    {
        const XmbGame &listed = *game.game;
        index += IndexLine({kGameRecord, game.key, listed.titleId, listed.title, GameInfo(listed),
                            Digits(listed.saves.size())});
        for (const SaveRow &row : game.saves)
        {
            const XmbSave &save = *row.save;
            index += IndexLine({kSaveRecord, row.key, save.name, save.title, save.info,
                                save.subtitle, save.updated, save.size, save.detail, save.titleId});
        }
    }
    return index;
}

} // namespace

XmbView ParseXmbView(const std::string &text)
{
    std::string letters = text;
    while (!letters.empty() && (letters.back() == '\n' || letters.back() == '\r'))
    {
        letters.pop_back();
    }
    if (letters.size() != 2)
    {
        return kDefaultView;
    }
    XmbView view = kDefaultView;
    bool sortRead = false;
    bool groupRead = false;
    for (const XmbSort sort : kSorts)
    {
        if (SortLetter(sort) == letters[0])
        {
            view.sort = sort;
            sortRead = true;
        }
    }
    for (const XmbGroup group : kGroups)
    {
        if (GroupLetter(group) == letters[1])
        {
            view.group = group;
            groupRead = true;
        }
    }
    return sortRead && groupRead ? view : kDefaultView;
}

std::string XmbMenuVariant(const XmbView &view)
{
    return VariantName(kMenuVariantStem, view.sort, view.group);
}

std::string XmbSavesVariant(const XmbView &view)
{
    return VariantName(kSavesVariantStem, view.sort, view.group);
}

std::string XmbGamesVariant(XmbSort sort)
{
    return VariantName(kGamesVariantStem, sort, XmbGroup::ByTitle);
}

XmbLists MakeXmbLists(const XmbPlace &place, bool signedIn, bool webman,
                      const std::string &storageUse, const std::vector<XmbGame> &games,
                      const XmbView &chosen)
{
    XmbLists lists;
    const MenuRows menu = MakeMenuRows(place, signedIn, webman, storageUse, lists.scripts);
    const std::vector<GameRow> rows = signedIn ? KeyRows(games) : std::vector<GameRow>();
    for (const GameRow &game : rows)
    {
        for (const SaveRow &row : game.saves)
        {
            if (Acts(webman, *row.save))
            {
                lists.scripts.push_back(XmbScript{row.key, xmbSave + row.save->name});
            }
        }
    }
    // A file comes after those it opens, so the XMB never opens one that is not written yet.
    const auto add = [&lists](const std::string &name, const std::string &content)
    { lists.files.push_back(XmbFile{name, content}); };
    for (const XmbSort sort : kSorts)
    {
        add(XmbGamesVariant(sort), GamesDocument(place, webman, rows, sort));
    }
    for (const XmbSort sort : kSorts)
    {
        for (const XmbGroup group : kGroups)
        {
            const XmbView view{sort, group};
            add(XmbSavesVariant(view),
                SavesDocument(place, webman, rows, view, XmbGamesVariant(sort)));
            add(XmbMenuVariant(view), MenuDocument(place, menu, XmbSavesVariant(view)));
        }
    }
    add(xmbGamesFile, GamesDocument(place, webman, rows, chosen.sort));
    add(xmbSavesFile, SavesDocument(place, webman, rows, chosen, xmbGamesFile));
    add(xmbMenuFile, MenuDocument(place, menu, xmbSavesFile));
    add(xmbFolderFile,
        Document(
            View(kFolderView,
                 Table("owns_folder", Pair("icon_rsc", kInCloudIcon) + Pair("title", kFolderTitle) +
                                          Pair("info_rsc", kFolderInfoRsc)),
                 FolderItem("owns_folder", ViewSource(place, xmbMenuFile, kMenuView)))));
    add(xmbRowsFile, RowsIndex(rows));
    return lists;
}

std::string XmbScriptText(const XmbPlace &place, const XmbScript &script)
{
    // A copy would keep the time the lists were written, and "log" adds to the file it names.
    // The launcher plugin starts the app as its icon does, whatever game is mounted; webMAN's own
    // start goes through /app_home, which a mounted game takes.
    return "del " + place.actionFile + "\nlogfile " + place.actionFile + "\nlog " + script.action +
           "\ndel " + place.launchRequest + "\nlogfile " + place.launchRequest + "\nlog launch\n";
}

std::string XmbCategoryEntry(const XmbPlace &place)
{
    return std::string("<Query class=\"type:x-xmb/folder-pixmap\" key=\"") + kCategoryKey +
           "\" src=\"" + Escaped(ViewSource(place, xmbFolderFile, kFolderView)) + "\"/>";
}

bool AddXmbCategoryEntry(std::string &category, const std::string &entry)
{
    if (category.find(std::string("key=\"") + kCategoryKey + "\"") != std::string::npos)
    {
        return false;
    }
    const std::string line = (category.find("\r\n") != std::string::npos ? "\r\n" : "\n") +
                             std::string("\t\t\t") + entry;
    bool added = false;
    // Every view of the Game column that lists the utility, as the XMB shows one or another.
    for (std::size_t utility = category.find(kSavedDataUtilityKey); utility != std::string::npos;
         utility = category.find(kSavedDataUtilityKey, utility + 1))
    {
        const std::size_t end = category.find("/>", utility);
        if (end != std::string::npos)
        {
            category.insert(end + 2, line);
            added = true;
        }
    }
    if (added)
    {
        return true;
    }
    const std::size_t root = category.find("<View id=\"root\">");
    const std::size_t items =
        root == std::string::npos ? std::string::npos : category.find("<Items>", root);
    if (items == std::string::npos)
    {
        return false;
    }
    category.insert(items + std::string("<Items>").size(), line);
    return true;
}

std::vector<XmbGame> XmbGames(SaveBrowser &browser, const LocalStorage &storage,
                              const std::string &savedata, const DateStyle &style)
{
    std::vector<XmbGame> games;
    for (const CloudGame &game : browser.Games())
    {
        XmbGame listed{game.name, game.titleId, {}};
        for (const std::string &name : game.saves)
        {
            const CloudSave &cloudSave = browser.Save(name);
            const std::string icon = savedata + "/" + name + "/" + kSaveIcon;
            FileStatus iconStatus;
            XmbSave save;
            save.name = name;
            save.title = game.name;
            save.subtitle = cloudSave.texts.subtitle.empty() ? name : cloudSave.texts.subtitle;
            save.updated = ShownTime(cloudSave.modified, style);
            save.size = ShownSize(cloudSave.size);
            // As the Saved Data Utility shows a save: the subtitle on a line of its own. The game's
            // ID is on its folder's row.
            save.info = save.subtitle + "\n" + save.updated + "   " + save.size;
            save.detail = ShownDetail(cloudSave.texts.detail);
            save.modified = cloudSave.modified;
            save.titleId = game.titleId;
            save.downloaded = browser.Status(name) == SaveStatus::Same;
            save.icon = save.downloaded && storage.Status(icon, iconStatus) ? icon : std::string();
            listed.saves.push_back(save);
        }
        games.push_back(listed);
    }
    return games;
}
