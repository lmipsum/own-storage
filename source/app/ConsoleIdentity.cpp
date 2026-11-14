// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ConsoleIdentity.h"

#include "app/DebugLog.h"
#include "app/Files.h"
#include "app/XRegistry.h"

#include <ppu-lv2.h>
#include <stdint.h>
#include <stdlib.h>

namespace
{
// The appliance info manager answers with the console's ids; it lets only custom firmware ask.
const u64 kApplianceInfoSyscall = 867;
const u32 kIdpsPacket = 0x19003;
const u32 kPsidPacket = 0x19005;
// Both ids are 16 bytes.
const std::size_t kConsoleIdSize = 16;

/** Asks the appliance info manager for one of the console's 16-byte ids. */
s32 ApplianceInfo(u32 packet, unsigned char *id)
{
    lv2syscall2(kApplianceInfoSyscall, packet, static_cast<u64>(reinterpret_cast<uintptr_t>(id)));
    return_to_user_prog(s32);
}

/** One of the console's ids; empty when it cannot be read. */
std::string ConsoleId(u32 packet)
{
    unsigned char id[kConsoleIdSize] = {};
    if (ApplianceInfo(packet, id) != 0)
    {
        return std::string();
    }
    return std::string(reinterpret_cast<const char *>(id), sizeof id);
}
} // namespace

std::optional<SaveTarget> ReadSaveTarget(const std::string &user)
{
    const std::string idps = ConsoleId(kIdpsPacket);
    const std::string psid = ConsoleId(kPsidPacket);
    if (idps.size() != kIdpsSize || psid.size() != kPsidSize)
    {
        debugPrintf("ReadSaveTarget: the console's ids cannot be read\n");
        return std::nullopt;
    }
    std::string registry;
    // Without the registry every save would look another user's and lose its account id.
    if (!ReadWholeFile(xRegistryPath, registry))
    {
        debugPrintf("ReadSaveTarget: the registry cannot be read\n");
        return std::nullopt;
    }
    const std::optional<std::string> accountId = FindAccountId(registry, user);
    // The registry holds PSN passwords too; they do not stay in memory.
    registry.assign(registry.size(), '\0');
    if (!accountId)
    {
        debugPrintf("ReadSaveTarget: the user's account id cannot be found\n");
        return std::nullopt;
    }
    // The console's C library offers strtoul only outside std.
    const u32 userId = static_cast<u32>(strtoul(user.c_str(), nullptr, 10));
    return SaveTarget{SaveOwner{userId, psid, *accountId}, idps};
}
