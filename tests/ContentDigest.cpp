// SPDX-License-Identifier: GPL-3.0-or-later
#include "ContentDigest.h"

#include <cstdint>
#include <cstdio>

std::string ContentDigest(const std::string &content)
{
    // FNV-1a, 64 bits.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : content)
    {
        hash = (hash ^ byte) * 1099511628211ULL;
    }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(hash));
    return hex;
}
