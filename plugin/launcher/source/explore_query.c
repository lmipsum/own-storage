// SPDX-License-Identifier: GPL-3.0-or-later
// explore_plugin (4.82/4.88) finds its open lists as its command runner does (ELF 0x70a60,
// 0x4973c, 0x1775c, 0x43848): the category manager M = *(0x2c96ac); its entries run from *(M+4)
// to *(M+8), 0xc bytes each, the first word an entry object E, the focused one with the byte E+8
// set; the current category C is the last pointer of E's vector *(E+0x10)..*(E+0x14); C's open
// lists are the pointers of its vector *(C+0x3c)..*(C+0x40), the deepest last; a list's stored
// query Q is *(list+0x20) (0x49214). Q's URL is the text [*(Q+0x0c), *(Q+0x10)), in place at
// Q+0x30 when short, else on the heap.
//
// The manager is not exported. Its address comes from where explore_plugin's data was loaded:
// interface 1, entry 1 returns a table at ELF 0x27ae88, once its code is checked to be the traced
// "lis r3; addi r3; blr". Each hop must stay in the VSH's heap and each vector be a sane size.
#include "explore_query.h"

#include <stddef.h>

#include "owns_log.h"
#include "paf.h"

#define EXPLORE_INTERFACE 1
#define DISPLAY_TABLE_ENTRY 1
#define EXEC_COMMAND_ENTRY 6
#define DISPLAY_TABLE_ELF 0x27ae88u
#define CATEGORY_MANAGER_ELF 0x2c96acu
#define HEAP_START 0x20000000u
#define HEAP_END 0x30000000u
#define MANAGER_ENTRIES 4
#define MANAGER_ENTRY_BYTES 0xc
#define ENTRY_FOCUSED 8
#define ENTRY_CATEGORIES 0x10
#define CATEGORY_LISTS 0x3c
#define LIST_QUERY 0x20
#define QUERY_URL_BEGIN 0x0c
#define QUERY_URL_END 0x10
#define QUERY_URL_IN_PLACE 0x30
#define VECTOR_MAX 64
// An instruction word, and the bits that must match: all, or the opcode and registers of a
// relocated immediate.
#define WHOLE 0xffffffffu
#define IMMEDIATE 0xffff0000u

typedef struct
{
    uint32_t word;
    uint32_t mask;
} code_word;

/** A function descriptor, as PPU function pointers point to: code address and TOC. */
typedef struct
{
    uint32_t code;
    uint32_t toc;
} function_descriptor;

// Interface 1, entry 1: lis r3, table@ha; addi r3, r3, table@l; blr.
static const code_word kTableGetter[] = {
    {0x3c600000u, IMMEDIATE},
    {0x38630000u, IMMEDIATE},
    {0x4e800020u, WHOLE},
};

static uint32_t word_at(uint32_t address)
{
    return *(const volatile uint32_t *)(uintptr_t)address;
}

static uint8_t byte_at(uint32_t address)
{
    return *(const volatile uint8_t *)(uintptr_t)address;
}

/** Whether an object's address is an aligned one in the VSH's heap. */
static int in_heap(uint32_t address)
{
    return address >= HEAP_START && address < HEAP_END - EXPLORE_URL_MAX && (address & 3) == 0;
}

/** explore_plugin's interface 1; NULL when it is not loaded. */
static const void *const *explore_interface(void)
{
    const int explore = View_Find("explore_plugin");
    return explore != 0 ? (const void *const *)plugin_GetInterface(explore, EXPLORE_INTERFACE)
                        : NULL;
}

/** The difference between where explore_plugin's data is and its ELF's; 0 when not as traced. */
static uint32_t data_delta(void)
{
    const void *const *entries = explore_interface();
    const function_descriptor *getter =
        entries != NULL ? (const function_descriptor *)entries[DISPLAY_TABLE_ENTRY] : NULL;
    if (getter == NULL)
    {
        return 0;
    }
    for (size_t i = 0; i < sizeof kTableGetter / sizeof kTableGetter[0]; ++i)
    {
        if ((word_at(getter->code + 4 * (uint32_t)i) & kTableGetter[i].mask) !=
            kTableGetter[i].word)
        {
            owns_log("explore_plugin not as traced");
            return 0;
        }
    }
    typedef const void *(*table_getter)(void);
    const uint32_t table = (uint32_t)(uintptr_t)((table_getter)(const void *)getter)();
    return table - DISPLAY_TABLE_ELF;
}

/**
 * A vector of pointers or entries: its begin and element count, when it is in the heap and of a
 * sane size.
 * @return its count; -1 when it is not so
 */
static int heap_vector(uint32_t at, uint32_t element, uint32_t *begin)
{
    *begin = word_at(at);
    const uint32_t end = word_at(at + 4);
    if (*begin == end)
    {
        return 0;
    }
    if (!in_heap(*begin) || end < *begin || (end - *begin) % element != 0 ||
        (end - *begin) / element > VECTOR_MAX)
    {
        return -1;
    }
    return (int)((end - *begin) / element);
}

/** The deepest open list of explore_plugin's current category; 0 when a hop is not as traced. */
static uint32_t deepest_list(void)
{
    const uint32_t delta = data_delta();
    const uint32_t manager = delta != 0 ? word_at(CATEGORY_MANAGER_ELF + delta) : 0;
    uint32_t entries = 0;
    const int entry_count =
        in_heap(manager) ? heap_vector(manager + MANAGER_ENTRIES, MANAGER_ENTRY_BYTES, &entries)
                         : -1;
    uint32_t focused = 0;
    for (int i = 0; i < entry_count && focused == 0; ++i)
    {
        const uint32_t entry = word_at(entries + MANAGER_ENTRY_BYTES * (uint32_t)i);
        if (in_heap(entry) && byte_at(entry + ENTRY_FOCUSED) != 0)
        {
            focused = entry;
        }
    }
    uint32_t categories = 0;
    const int category_count =
        focused != 0 ? heap_vector(focused + ENTRY_CATEGORIES, 4, &categories) : -1;
    const uint32_t category =
        category_count > 0 ? word_at(categories + 4 * (uint32_t)(category_count - 1)) : 0;
    uint32_t lists = 0;
    const int list_count =
        in_heap(category) ? heap_vector(category + CATEGORY_LISTS, 4, &lists) : -1;
    const uint32_t list = list_count > 0 ? word_at(lists + 4 * (uint32_t)(list_count - 1)) : 0;
    owns_log_value("deepest open list", list);
    return in_heap(list) ? list : 0;
}

int explore_query_url(explore_url *url)
{
    const uint32_t list = deepest_list();
    const uint32_t query = list != 0 ? word_at(list + LIST_QUERY) : 0;
    if (!in_heap(query))
    {
        return 0;
    }
    const uint32_t begin = word_at(query + QUERY_URL_BEGIN);
    const uint32_t end = word_at(query + QUERY_URL_END);
    const int placed = begin == query + QUERY_URL_IN_PLACE ||
                       (begin >= HEAP_START && begin < HEAP_END - EXPLORE_URL_MAX);
    if (!placed || end <= begin || end - begin >= EXPLORE_URL_MAX)
    {
        return 0;
    }
    static const char kScheme[] = "xmb://";
    for (uint32_t i = 0; i + 1 < sizeof kScheme; ++i)
    {
        if (byte_at(begin + i) != (uint8_t)kScheme[i])
        {
            return 0;
        }
    }
    url->address = begin;
    url->length = end - begin;
    return 1;
}

void explore_url_copy(const explore_url *url, char *text)
{
    uint32_t i = 0;
    for (; i < url->length && i + 1 < EXPLORE_URL_MAX; ++i)
    {
        text[i] = (char)byte_at(url->address + i);
    }
    text[i] = '\0';
}

int explore_command(const char *command)
{
    typedef int (*exec_command)(const char *command, void *callback, int argument);
    const void *const *entries = explore_interface();
    const exec_command exec =
        entries != NULL ? (exec_command)entries[EXEC_COMMAND_ENTRY] : (exec_command)NULL;
    return exec != NULL && exec(command, NULL, 0) != 0;
}
