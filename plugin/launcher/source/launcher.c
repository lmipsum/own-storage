// SPDX-License-Identifier: GPL-3.0-or-later
// The XMB's rows start Own Storage through this VSH plugin, the way its own icon starts it: the
// system's game_ext_plugin launches the installed app, whatever disc or game is mounted. The app's
// results show through it too, as the XMB's notifications, in the system's words where it has them.
//
// The module's only segment is read-only once loaded, as the PS3 refuses to load a segment that
// is both writable and executable; so nothing here writes to a global, and the thread runs until
// the console shuts down.

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prx_module.h>
#include <sys/systime.h>
#include <sys/thread.h>

/** paf's View_Find: a loaded plugin's view; 0 when the plugin is not loaded. */
extern int View_Find(const char *name);
/** paf's plugin_GetInterface: one of a plugin's interfaces; NULL when it has none of the id. */
extern void *plugin_GetInterface(int view, int id);
/** paf's View::GetString: a plugin's text in the system's language, as UTF-16; NULL without it. */
extern const unsigned short *View_GetString(int view, const char *label);
/** vshtask's notification, which shows a line of UTF-8 in the XMB's corner. */
extern int vshtask_notify(int arg, const char *message);

// xmb_plugin's interface that loads other plugins, and game_ext_plugin's id among them, as webMAN
// MOD's vsh/xmb_plugin.h describes them.
#define XMM0_INTERFACE 0x584d4d30
#define GAME_EXT_PLUGIN 0x16

/** The start of xmb_plugin's XMM0 interface. */
typedef struct
{
    int (*GetPluginIdByName)(const char *);
    char *(*GetPluginNameById)(int);
    int (*IsPluginViewAvailable)(int);
    /** Loads a plugin, then calls the handler once its view is up. */
    int (*LoadPlugin3)(int, void *, int);
} xmm0_interface;

/** game_ext_plugin's interface 1, entry 3: starts the installed game in a folder. */
typedef int (*select_by_folder)(const char *folder, int place);
#define SELECT_BY_FOLDER 3
// The folder is one in /dev_hdd0/game.
#define HDD_GAME 2

// The file a row's script writes to ask for the app, as the app's xmbPlace names it; the plugin
// removes it as it starts the app.
static const char kRequest[] = "/dev_hdd0/tmp/owns_launch";
static const char kAppFolder[] = "OWNS00001";
// The file the app writes its results to, a notification a line, as the app's notificationRequest
// names it; a {plugin:label} part stands for that plugin's text in the system's language.
static const char kNotices[] = "/dev_hdd0/tmp/owns_notify";
// How much of the file and of a notification the stack holds; more is left out.
#define NOTICES_BYTES 1024
#define LINE_BYTES 512
#define NAME_BYTES 64
static const unsigned int kPollMicroseconds = 300000;
// The watching thread waits most of the time; a small stack holds what it needs.
static const s32 kThreadPriority = 3000;
static const u64 kThreadStackBytes = 0x4000;

/** Starts the app, once game_ext_plugin is loaded. */
static void on_game_ext_loaded(void)
{
    const int view = View_Find("game_ext_plugin");
    void **entries = view != 0 ? (void **)plugin_GetInterface(view, 1) : NULL;
    if (entries != NULL)
    {
        ((select_by_folder)entries[SELECT_BY_FOLDER])(kAppFolder, HDD_GAME);
    }
}

/** Has game_ext_plugin loaded, which then starts the app. */
static void start_app(void)
{
    // Loaded already, it would not call the handler, and the request is dropped; the XMB unloads
    // it once the app has started, so the next request starts the app again.
    if (View_Find("game_ext_plugin") != 0)
    {
        return;
    }
    const int view = View_Find("xmb_plugin");
    xmm0_interface *xmm0 =
        view != 0 ? (xmm0_interface *)plugin_GetInterface(view, XMM0_INTERFACE) : NULL;
    if (xmm0 != NULL)
    {
        xmm0->LoadPlugin3(GAME_EXT_PLUGIN, (void *)on_game_ext_loaded, 0);
    }
}

/**
 * Adds a character to a UTF-8 text, when there is room for it and the closing NUL.
 * @return the text's new length
 */
static size_t put_utf8(char *text, size_t length, size_t room, unsigned int code)
{
    const size_t bytes = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    if (length + bytes >= room)
    {
        return length;
    }
    if (bytes == 1)
    {
        text[length] = (char)code;
        return length + 1;
    }
    // The lead byte's marks for 2, 3 and 4 bytes, then six bits a continuation byte.
    static const unsigned char kLead[] = {0, 0, 0xC0, 0xE0, 0xF0};
    for (size_t i = bytes - 1; i > 0; --i)
    {
        text[length + i] = (char)(0x80 | (code & 0x3F));
        code >>= 6;
    }
    text[length] = (char)(kLead[bytes] | code);
    return length + bytes;
}

/**
 * Adds a plugin's text in the system's language to a UTF-8 text.
 * @param name "plugin:label", not NUL-ended
 * @param size the name's bytes
 * @return the text's new length
 */
static size_t put_system_text(char *text, size_t length, size_t room, const char *name, size_t size)
{
    char plugin[NAME_BYTES];
    char label[NAME_BYTES];
    size_t colon = 0;
    while (colon < size && name[colon] != ':')
    {
        ++colon;
    }
    if (colon >= NAME_BYTES || colon == size || size - colon > NAME_BYTES)
    {
        return length;
    }
    for (size_t i = 0; i < colon; ++i)
    {
        plugin[i] = name[i];
    }
    plugin[colon] = '\0';
    for (size_t i = colon + 1; i < size; ++i)
    {
        label[i - colon - 1] = name[i];
    }
    label[size - colon - 1] = '\0';
    const int view = View_Find(plugin);
    const unsigned short *units = view != 0 ? View_GetString(view, label) : NULL;
    for (size_t i = 0; units != NULL && units[i] != 0; ++i)
    {
        unsigned int code = units[i];
        // A surrogate pair stands for a character beyond the first 65536.
        if (code >= 0xD800 && code < 0xDC00 && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000)
        {
            code = 0x10000 + ((code - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        }
        // The system's texts break their lines; a notification is one line.
        length = put_utf8(text, length, room, code == '\n' ? ' ' : code);
    }
    return length;
}

/** Shows one line of the app's as a notification, its {plugin:label} parts in system words. */
static void notify(const char *line, size_t size)
{
    char text[LINE_BYTES];
    size_t length = 0;
    for (size_t i = 0; i < size; ++i)
    {
        size_t end = i;
        if (line[i] == '{')
        {
            while (end < size && line[end] != '}')
            {
                ++end;
            }
        }
        if (line[i] == '{' && end < size)
        {
            length = put_system_text(text, length, sizeof text, line + i + 1, end - i - 1);
            i = end;
        }
        else if (length + 1 < sizeof text)
        {
            text[length++] = line[i];
        }
    }
    text[length] = '\0';
    if (length > 0)
    {
        vshtask_notify(0, text);
    }
}

/** Shows the app's results, a notification a line, and removes them. */
static void show_notices(void)
{
    char notices[NOTICES_BYTES];
    uint64_t size = 0;
    int file;
    if (cellFsOpen(kNotices, CELL_FS_O_RDONLY, &file, NULL, 0) != 0)
    {
        return;
    }
    cellFsRead(file, notices, sizeof notices, &size);
    cellFsClose(file);
    cellFsUnlink(kNotices);
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i)
    {
        if (i == size || notices[i] == '\n')
        {
            notify(notices + start, i - start);
            start = i + 1;
        }
    }
}

/** Waits for a row's request, starting the app for each, and for the app's results. */
static void watch(void *unused)
{
    (void)unused;
    // A request left from before the console started, as when the user used a row before the
    // restart that loads this plugin, would start the app unasked.
    cellFsUnlink(kRequest);
    // Results left so would be shown as new.
    cellFsUnlink(kNotices);
    for (;;)
    {
        CellFsStat status;
        if (cellFsStat(kRequest, &status) == 0)
        {
            cellFsUnlink(kRequest);
            start_app();
        }
        if (cellFsStat(kNotices, &status) == 0)
        {
            show_notices();
        }
        sysUsleep(kPollMicroseconds);
    }
}

int module_start(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    sys_ppu_thread_t thread;
    sysThreadCreate(&thread, watch, NULL, kThreadPriority, kThreadStackBytes, 0,
                    (char *)"owns_launcher");
    return SYS_PRX_RESIDENT;
}

int module_stop(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    return SYS_PRX_RESIDENT;
}
