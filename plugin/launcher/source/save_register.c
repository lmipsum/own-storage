// SPDX-License-Identifier: GPL-3.0-or-later
#include "save_register.h"

#include <cell/fs/cell_fs_file_api.h>
#include <stddef.h>
#include <sys/systime.h>

#include "explore_query.h"
#include "owns_log.h"
#include "owns_paths.h"
#include "owns_text.h"
#include "vsh_types.h"

// The request while its batch is registered: the app's file renamed, so the app writes a new one.
#define REGISTER_TAKING OWNS_REGISTER_REQUEST ".taking"
// Far more than one run of the app downloads; what lies beyond is left out.
#define REQUEST_MAX_BYTES (64 * 1024)
// The commands one round sends: each waits for explore_plugin, and Triangle waits for the round.
#define COMMANDS_PER_ROUND 4
// A pause after each command taken, so that the system writes its database for one folder before
// the runner is handed the next.
#define PACE_MICROSECONDS 100000
// A folder whose command explore_plugin does not take in this many rounds is given up, so that a
// runner that stays busy does not hold up the rest for good.
#define REFUSALS_MAX 3
// The only folders registered: /dev_hdd0/home/<user, 8 digits>/savedata/<save name>, the folders
// the app downloads into, so the command never names another place.
#define FOLDER_HOME "/dev_hdd0/home/"
#define FOLDER_USER_DIGITS 8
#define FOLDER_SAVEDATA "/savedata/"
#define FOLDER_NAME_MAX 31
#define COMMAND_START "metagen_sync directory "
#define COMMAND_END " 1"
// How much of a refused line the log shows.
#define LOGGED_LINE_BYTES 96

/** Logs a line of the request, which is not NUL-ended, after a note. */
static void log_line(const char *note, const char *line, uint32_t length)
{
    char text[LOGGED_LINE_BYTES];
    size_t used = owns_text_append(text, 0, sizeof text, note);
    for (uint32_t i = 0; i < length && used + 1 < sizeof text; ++i)
    {
        text[used++] = line[i];
    }
    text[used] = '\0';
    owns_log(text);
}

/** Whether a text follows at a line's offset; the offset then moves past it. */
static int take_text(const char *line, uint32_t length, uint32_t *at, const char *text)
{
    for (size_t i = 0; text[i] != '\0'; ++i, ++*at)
    {
        if (*at >= length || line[*at] != text[i])
        {
            return 0;
        }
    }
    return 1;
}

/** Whether a byte may be in a save folder's name: A-Z, 0-9, '_' and '-', as the system's are. */
static int name_byte(char byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' ||
           byte == '-';
}

/** Whether a line is exactly a save folder of a user's savedata, and nothing else. */
static int folder_valid(const char *line, uint32_t length)
{
    uint32_t at = 0;
    if (!take_text(line, length, &at, FOLDER_HOME))
    {
        return 0;
    }
    for (int i = 0; i < FOLDER_USER_DIGITS; ++i, ++at)
    {
        if (at >= length || line[at] < '0' || line[at] > '9')
        {
            return 0;
        }
    }
    if (!take_text(line, length, &at, FOLDER_SAVEDATA) || at == length ||
        length - at > FOLDER_NAME_MAX)
    {
        return 0;
    }
    for (; at < length; ++at)
    {
        if (!name_byte(line[at]))
        {
            return 0;
        }
    }
    return 1;
}

/** Whether two texts of a length have the same bytes. */
static int same_bytes(const char *a, const char *b, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
    }
    return 1;
}

/** Whether a line of the batch came before it too; every line before it ends in '\n'. */
static int seen_before(const char *batch, uint32_t start, uint32_t length)
{
    for (uint32_t at = 0; at < start;)
    {
        uint32_t end = at;
        while (batch[end] != '\n')
        {
            ++end;
        }
        if (end - at == length && same_bytes(batch + at, batch + start, length))
        {
            return 1;
        }
        at = end + 1;
    }
    return 0;
}

/**
 * Takes the app's request. It is renamed first, so that what the app adds from then on goes to a
 * new request rather than into a file being read; a renamed one left by a shutdown during its batch
 * is taken first. Its whole lines go onto the VSH's heap, as they may be too many for the thread's
 * stack; the renamed file stays until its batch is done.
 * @return 0 when there is none, or nothing whole in it
 */
static int take_request(save_register *registry)
{
    CellFsStat status;
    if (cellFsStat(REGISTER_TAKING, &status) != 0 &&
        (cellFsRename(OWNS_REGISTER_REQUEST, REGISTER_TAKING) != 0 ||
         cellFsStat(REGISTER_TAKING, &status) != 0))
    {
        return 0;
    }
    const uint64_t bytes = status.st_size < REQUEST_MAX_BYTES ? status.st_size : REQUEST_MAX_BYTES;
    char *batch = (char *)vsh_new((unsigned int)bytes + 1);
    if (batch == NULL)
    {
        // Kept for the next round, when the heap may have room again.
        return 0;
    }
    const int64_t read = owns_read_file(REGISTER_TAKING, batch, bytes + 1);
    // A line the app wrote ends in '\n'; one cut short by the size limit would name another folder.
    uint32_t whole = 0;
    for (int64_t i = 0; i < read; ++i)
    {
        if (batch[i] == '\n')
        {
            whole = (uint32_t)i + 1;
        }
    }
    if (whole == 0)
    {
        vsh_delete(batch);
        cellFsUnlink(REGISTER_TAKING);
        return 0;
    }
    registry->batch = batch;
    registry->length = whole;
    registry->next = 0;
    registry->refusals = 0;
    owns_log_value("register request bytes", whole);
    return 1;
}

/**
 * Hands explore_plugin the command that registers a folder, written in the slot the command taken
 * last is not in.
 * @param folder a line that folder_valid took, so the command fits its slot
 * @return 1 when explore_plugin took it
 */
static int send_command(save_register *registry, const char *folder, uint32_t length)
{
    char *command = registry->commands[registry->slot];
    size_t used = owns_text_append(command, 0, SAVE_REGISTER_COMMAND_BYTES, COMMAND_START);
    for (uint32_t i = 0; i < length && used + 1 < SAVE_REGISTER_COMMAND_BYTES; ++i)
    {
        command[used++] = folder[i];
    }
    command[used] = '\0';
    owns_text_append(command, used, SAVE_REGISTER_COMMAND_BYTES, COMMAND_END);
    if (!explore_command_wait(command))
    {
        return 0;
    }
    registry->slot = (registry->slot + 1) % SAVE_REGISTER_SLOTS;
    return 1;
}

/**
 * Registers one line's folder, unless it is no save folder or came before in the batch.
 * @return 0 when its command was not taken, to be tried again in the next round
 */
static int register_line(save_register *registry, const char *line, uint32_t length, int *sent)
{
    if (!folder_valid(line, length))
    {
        log_line("not a save folder: ", line, length);
        return 1;
    }
    if (seen_before(registry->batch, (uint32_t)(line - registry->batch), length))
    {
        log_line("listed before: ", line, length);
        return 1;
    }
    if (send_command(registry, line, length))
    {
        log_line("save registered: ", line, length);
        ++*sent;
        sysUsleep(PACE_MICROSECONDS);
        return 1;
    }
    if (++registry->refusals < REFUSALS_MAX)
    {
        return 0;
    }
    log_line("save not registered: ", line, length);
    return 1;
}

void save_register_init(save_register *registry)
{
    registry->batch = NULL;
    registry->length = 0;
    registry->next = 0;
    registry->refusals = 0;
    registry->slot = 0;
}

int save_register_waiting(const save_register *registry)
{
    return registry->batch != NULL || owns_file_exists(REGISTER_TAKING) ||
           owns_file_exists(OWNS_REGISTER_REQUEST);
}

void save_register_round(save_register *registry)
{
    if (registry->batch == NULL && !take_request(registry))
    {
        return;
    }
    int sent = 0;
    while (registry->next < registry->length && sent < COMMANDS_PER_ROUND)
    {
        const char *line = registry->batch + registry->next;
        uint32_t length = 0;
        while (line[length] != '\n')
        {
            ++length;
        }
        if (!register_line(registry, line, length, &sent))
        {
            return;
        }
        registry->refusals = 0;
        registry->next += length + 1;
    }
    if (registry->next >= registry->length)
    {
        vsh_delete(registry->batch);
        registry->batch = NULL;
        cellFsUnlink(REGISTER_TAKING);
    }
}
