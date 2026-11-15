// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * Whether the console runs Cobra, which loads the plugins its boot list names; without it the
 * XMB's rows cannot start the app.
 */
bool CobraRuns();

/** Restarts the console; it does not return when the system takes the request. */
void RestartConsole();
