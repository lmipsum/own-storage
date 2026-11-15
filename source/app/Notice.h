// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/** What came of something the app did, as the user is told it. */
struct Notice
{
    /** The app's own words, which a dialog shows. */
    std::string text;
    /**
     * The system's words for it, which a notification shows in the system's language: text with
     * {plugin:label} parts, which the launcher plugin fills in with the system's texts; empty when
     * the system has none, so the notification shows text.
     */
    std::string system;
    /** Whether something went wrong, which a dialog shows rather than a notification. */
    bool problem;
};

/**
 * A notice of a result, which a notification shows.
 * @param text the app's own words
 * @param system the system's words for it; empty when it has none
 */
Notice Result(const std::string &text, const std::string &system = std::string());

/** A notice of a problem, which a dialog shows so it is not missed. */
Notice Problem(const std::string &text);

/**
 * The line the launcher plugin shows as a notification: the system's words, or else the app's, on
 * one line, as each line of the request is a notification of its own.
 */
std::string NotificationLine(const Notice &notice);
