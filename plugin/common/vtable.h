// SPDX-License-Identifier: GPL-3.0-or-later
// paf's objects are C++ ones: their virtual functions are called through their vtables, by slot.

#ifndef OWNS_VTABLE_H
#define OWNS_VTABLE_H

#include <stdint.h>

/** A virtual function of a paf object, by its slot in the object's vtable. */
static inline const void *virtual_function(const void *object, uint32_t slot)
{
    const uint32_t vtable = *(const uint32_t *)object;
    return *(const void *const *)(uintptr_t)(vtable + slot);
}

/** A word of a paf object, read as it is now. */
static inline uint32_t object_word(const void *object, uint32_t offset)
{
    return *(const volatile uint32_t *)((uintptr_t)object + offset);
}

#endif
