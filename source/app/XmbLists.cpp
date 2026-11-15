// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XmbLists.h"

#include <sstream>

namespace
{
// The view of a list file that the XMB opens.
const char kFolderView[] = "owns_folder_view";
const char kMenuView[] = "owns_menu_view";
const char kSavesView[] = "owns_saves_view";
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
// without an icon of its own, which the folder of the saves in the cloud shares; and a save only
// in the cloud, which the Cloud Save Utility folder and the row that asks for webMAN share.
const char kSyncIcon[] = "tex_loading_icon";
const char kUsbIcon[] = "item_tex_usb_connection";
const char kAccountIcon[] = "tex_Avatar_Default";
const char kDownloadedIcon[] = "item_tex_ps3util";
const char kInCloudIcon[] = "tex_album_icon";
// The icon file of a save folder.
const char kSaveIcon[] = "ICON0.PNG";
// The longest name the console gives a save folder.
const std::size_t kSaveNameMaxBytes = 31;

/** A count in digits; the console's C++ library offers no std::to_string. */
std::string Digits(std::size_t count)
{
    std::ostringstream text;
    text << count;
    return text.str();
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
std::string ViewSource(const XmbPlace &place, const char *file, const std::string &view)
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

} // namespace

XmbLists MakeXmbLists(const XmbPlace &place, bool signedIn, bool webman,
                      const std::vector<XmbSave> &saves)
{
    XmbLists lists;
    lists.folder = Document(
        View(kFolderView,
             Table("owns_folder", Pair("icon_rsc", kInCloudIcon) + Pair("title", kFolderTitle) +
                                      Pair("info_rsc", kFolderInfoRsc)),
             FolderItem("owns_folder", ViewSource(place, xmbMenuFile, kMenuView))));

    std::string tables;
    std::string items;
    // A row's title is a system text; its description, when it has one, is the app's own.
    const auto addAction = [&](const char *key, const char *icon, const char *title,
                               const char *info, const char *action)
    {
        if (!webman)
        {
            return;
        }
        const std::string described = info != nullptr ? Pair("info", info) : std::string();
        tables += Table(key, Pair("icon_rsc", icon) + Pair("title_rsc", title) + described +
                                 ActionPairs(place, key));
        items += ActionItem(key);
        lists.scripts.push_back(XmbScript{key, action});
    };
    if (!webman)
    {
        tables += Table("owns_webman",
                        Pair("icon_rsc", kInCloudIcon) + Pair("title", "Install webMAN MOD") +
                            Pair("info", "The rows here need it to start Own Storage."));
        items += ActionItem("owns_webman");
    }
    if (!signedIn)
    {
        addAction("owns_signin", kAccountIcon, kSignInRsc, nullptr, xmbSignIn);
    }
    else
    {
        addAction("owns_sync", kSyncIcon, kSyncRsc, nullptr, xmbSync);
        addAction("owns_usb", kUsbIcon, kUsbRsc, kUsbInfo, xmbUsbImport);
        tables += Table("owns_cloud",
                        Pair("icon_rsc", kDownloadedIcon) + Pair("title_rsc", kCloudSavesRsc));
        items += FolderItem("owns_cloud", ViewSource(place, xmbSavesFile, kSavesView));
        addAction("owns_signout", kAccountIcon, kSignOutRsc, nullptr, xmbSignOut);
    }
    lists.menu = Document(View(kMenuView, tables, items));

    tables.clear();
    items.clear();
    for (std::size_t s = 0; signedIn && s < saves.size(); ++s)
    {
        const XmbSave &save = saves[s];
        const std::string key = kSaveKeyPrefix + Digits(s);
        // A save of a name no console gives is only listed.
        const bool acts = webman && IsSaveFolderName(save.name);
        const std::string actionPairs = acts ? ActionPairs(place, key) : std::string();
        tables += Table(key, IconPair(save.icon, save.downloaded ? kDownloadedIcon : kInCloudIcon) +
                                 Pair("title", save.title) + Pair("info", save.info) + actionPairs);
        items += ActionItem(key);
        if (acts)
        {
            lists.scripts.push_back(XmbScript{key, xmbSave + save.name});
        }
    }
    lists.saves = Document(View(kSavesView, tables, items));
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

std::vector<XmbSave> XmbSaves(SaveBrowser &browser, const LocalStorage &storage,
                              const std::string &savedata, const DateStyle &style)
{
    std::vector<XmbSave> saves;
    for (const CloudGame &game : browser.Games())
    {
        for (const std::string &name : game.saves)
        {
            const CloudSave &cloudSave = browser.Save(name);
            const std::string icon = savedata + "/" + name + "/" + kSaveIcon;
            FileStatus iconStatus;
            XmbSave save;
            save.name = name;
            // As the Saved Data Utility shows a save: the subtitle on a line of its own.
            save.title = game.name;
            save.info = (cloudSave.texts.subtitle.empty() ? name : cloudSave.texts.subtitle) +
                        "\n" + game.titleId + " | " + ShownTime(cloudSave.modified, style) + "   " +
                        ShownSize(cloudSave.size);
            save.downloaded = browser.Status(name) == SaveStatus::Same;
            save.icon = save.downloaded && storage.Status(icon, iconStatus) ? icon : std::string();
            saves.push_back(save);
        }
    }
    return saves;
}
