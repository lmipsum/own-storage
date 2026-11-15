// SPDX-License-Identifier: GPL-3.0-or-later
// The system's texts are UTF-16; notifications and the app's files are UTF-8.

#ifndef OWNS_UTF8_H
#define OWNS_UTF8_H

#include <stddef.h>

/**
 * Adds a character to a UTF-8 text, when there is room for it and the closing NUL.
 * @param length the text's length
 * @param room the text's buffer bytes
 * @param code the character's code point
 * @return the text's new length
 */
size_t utf8_put(char *text, size_t length, size_t room, unsigned int code);

/**
 * The character a UTF-16 text has at an index, joining a surrogate pair into one.
 * @param units the text
 * @param count its units
 * @param index the character's first unit; moves past the character
 * @return the character's code point
 */
unsigned int utf16_next(const unsigned short *units, size_t count, size_t *index);

#endif
