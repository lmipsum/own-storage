// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Cobra.h"

#include "app/DebugLog.h"

#include <ppu-lv2.h>
#include <stdint.h>
#include <sys/file.h>

namespace
{
// Cobra answers on syscall 8; asked for its version, it fails on a console without it.
const u64 kCobraSyscall = 8;
const u64 kCobraVersion = 0x7000;
// sys_sm_shutdown, asked to restart, as webMAN MOD's installer restarts the console.
const u64 kShutdownSyscall = 379;
const u64 kRestart = 0x8201;
// The system writes this as it starts and removes it as it shuts down; found at the next start, it
// makes the system say it was not turned off properly. webMAN MOD removes it before it restarts.
const char kRunningMark[] = "/dev_hdd0/tmp/turnoff";

/** Asks Cobra for its version; negative without Cobra. */
s32 CobraVersion(u32 *version)
{
    lv2syscall2(kCobraSyscall, kCobraVersion,
                static_cast<u64>(reinterpret_cast<uintptr_t>(version)));
    return_to_user_prog(s32);
}

/** Asks the system to shut down in the given way; it answers only when it refuses. */
s32 Shutdown(u64 action)
{
    lv2syscall3(kShutdownSyscall, action, 0, 0);
    return_to_user_prog(s32);
}
} // namespace

bool CobraRuns()
{
    u32 version = 0;
    return CobraVersion(&version) >= 0;
}

void RestartConsole()
{
    sysFsUnlink(kRunningMark);
    const s32 refused = Shutdown(kRestart);
    debugPrintf("RestartConsole: the system refused with %x\n", static_cast<unsigned int>(refused));
}
