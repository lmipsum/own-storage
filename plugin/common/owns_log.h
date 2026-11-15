// SPDX-License-Identifier: GPL-3.0-or-later
// The plugin modules' log, a line each call, in OWNS_LOG_FILE. Only a build with OWNS_PLUGIN_LOG
// writes it; otherwise the calls compile to nothing, and a release writes no file.

#ifndef OWNS_LOG_H
#define OWNS_LOG_H

#include <stdint.h>

#define OWNS_LOG_FILE "/dev_hdd0/tmp/owns_menu.log"

#if OWNS_PLUGIN_LOG
/** Appends a line to the log. */
void owns_log(const char *text);
/** Appends a line to the log: the text, then the value in hex. */
void owns_log_value(const char *text, uint32_t value);
#else
static inline void owns_log(const char *text)
{
    (void)text;
}
static inline void owns_log_value(const char *text, uint32_t value)
{
    (void)text;
    (void)value;
}
#endif

#endif
