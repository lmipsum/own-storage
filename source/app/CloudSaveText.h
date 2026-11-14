// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/SaveBrowser.h"

#include <string>

/** A size the way a person reads it, such as "4.4 MB". */
std::string ShownSize(unsigned long long bytes);

/**
 * Drive's time of a change the way a person reads it.
 * @param modified such as "2026-11-13T21:13:21.672Z"
 * @return such as "2026-11-13 21:13 UTC"; the time as it is when it is shorter than that
 */
std::string ShownTime(const std::string &modified);

/** What a save's status means to the user. */
const char *ShownStatus(SaveStatus status);

/** What loading a save came to, as the user reads it. */
std::string ShownResult(const std::string &name, LoadResult result);

/** What deleting a save from the cloud came to, as the user reads it. */
std::string ShownResult(const std::string &name, RemoveResult result);
