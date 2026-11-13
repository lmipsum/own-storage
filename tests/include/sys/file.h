// SPDX-License-Identifier: GPL-3.0-or-later
// Stands in for PSL1GHT's <sys/file.h> when the shared code is built for the tests on a PC: it
// offers the console file calls that code makes, done with the PC's own.
#pragma once

#include <sys/stat.h>

inline int sysFsChmod(const char *path, int mode)
{
    return chmod(path, static_cast<mode_t>(mode));
}
