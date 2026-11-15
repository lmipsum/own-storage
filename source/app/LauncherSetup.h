// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sync/LocalStorage.h"

#include <string>

/** Where the XMB's launcher plugin comes from and where Cobra loads it from. */
struct LauncherPlace
{
    /** The plugin as the app's package brings it. */
    std::string bundled;
    /** Where Cobra loads it from, as its line in the boot list names it. */
    std::string installed;
    /** Cobra's list of the plugins it loads as the console starts. */
    std::string bootList;
    /** A copy of the boot list as it was before the app first added to it. */
    std::string bootListBackup;
};

/** What setting up the launcher came to. */
enum class LauncherSetup
{
    /** It was in place: what runs is what the app brings. */
    Unchanged,
    /** It was put in place, and runs from the console's next start. */
    Changed,
    /** It could not be put in place. */
    Failed
};

/**
 * A boot list with a plugin's line added at its end, unless it has the line; every other line,
 * webMAN MOD's among them, stays as it is.
 * @param list the boot list; empty when there is none
 * @param path the plugin's path
 * @return the list with the line
 */
std::string WithBootLine(const std::string &list, const std::string &path);

/**
 * Puts the launcher plugin in place, so the XMB's rows can start the app. The plugin is copied
 * when the installed one differs from the app's, as after an update; its line is added to the boot
 * list when it lacks it, the old list kept as the backup unless one is kept already.
 * @param storage the console's hard disk
 * @param place where the plugin and the boot list are
 * @return whether it changed, and so runs only from the next start
 */
LauncherSetup SetUpLauncher(LocalStorage &storage, const LauncherPlace &place);
