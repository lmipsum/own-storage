// SPDX-License-Identifier: GPL-3.0-or-later
// Small text and file helpers; the modules link no C library that would have them.

#ifndef OWNS_TEXT_H
#define OWNS_TEXT_H

#include <stddef.h>
#include <stdint.h>

/** A NUL-ended text's length. */
size_t owns_text_length(const char *text);

/**
 * Appends a text to another, as much of it as fits with the closing NUL.
 * @param length the other text's length
 * @param room the other text's buffer bytes
 * @return the other text's new length
 */
size_t owns_text_append(char *text, size_t length, size_t room, const char *part);

/**
 * Reads a whole small file into a buffer, NUL-ended; what does not fit is left out.
 * @param room the buffer's bytes
 * @return the bytes read; -1 when the file cannot be read
 */
int64_t owns_read_file(const char *path, char *buffer, uint64_t room);

/**
 * Writes a text as a whole file, replacing what was there.
 * @return 1 when all of it was written
 */
int owns_write_file(const char *path, const char *text);

#endif
