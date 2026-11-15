// SPDX-License-Identifier: GPL-3.0-or-later
// The app's results, shown as the XMB's notifications, in the system's words where it has them.

#ifndef OWNS_NOTICES_H
#define OWNS_NOTICES_H

#include <stdint.h>

// The file the app writes its results to, a notification a line, as the app's notificationRequest
// names it; a {plugin:label} part stands for that plugin's text in the system's language.
#define NOTICES_FILE "/dev_hdd0/tmp/owns_notify"
// How much of the file the caller holds; more is left out.
#define NOTICES_BYTES 1024

/**
 * Takes the app's results from their file, which tells the app they are taken, after those taken
 * before.
 * @param notices the results taken so far, NOTICES_BYTES
 * @param size their bytes; grows by those taken now
 */
void notices_take(char *notices, uint64_t *size);

/** Shows results, a notification a line. */
void notices_show(const char *notices, uint64_t size);

#endif
