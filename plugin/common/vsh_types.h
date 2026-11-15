// SPDX-License-Identifier: GPL-3.0-or-later
// The VSH's own C++ strings, as paf takes and fills them, and the allocator they live on (stubs/
// allocator/exports.yml). Both are Dinkumware's: a short text in place, a longer one on the heap.

#ifndef OWNS_VSH_TYPES_H
#define OWNS_VSH_TYPES_H

#include <stdint.h>

/** VSH std::string: in place up to a capacity of 15, else a pointer to the heap; NUL-ended. */
typedef struct
{
    uint32_t allocator;
    union
    {
        char text[16];
        char *heap;
    } data;
    uint32_t size;
    uint32_t capacity;
} vsh_string;

#define VSH_STRING_IN_PLACE 15

/** VSH std::wstring, of UTF-16 units: in place up to a capacity of 7; NUL-ended. */
typedef struct
{
    uint32_t allocator;
    union
    {
        unsigned short text[8];
        unsigned short *heap;
    } data;
    uint32_t size;
    uint32_t capacity;
} vsh_wstring;

#define VSH_WSTRING_IN_PLACE 7

_Static_assert(sizeof(void *) == 4, "the VSH's pointers are 32-bit");
_Static_assert(sizeof(vsh_string) == 0x1c, "VSH std::string is 0x1c bytes");
_Static_assert(sizeof(vsh_wstring) == 0x1c, "VSH std::wstring is 0x1c bytes");

/** The VSH's operator new, which paf's strings use. */
void *vsh_new(unsigned int size);
/** The VSH's operator delete. */
void vsh_delete(void *block);

#endif
