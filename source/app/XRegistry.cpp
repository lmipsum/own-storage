// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/XRegistry.h"

#include "sync/ParamSfo.h"

#include <cstddef>

namespace
{
// The layout psdevwiki.com describes (XRegistry.sys); its numbers are big-endian. The key table
// starts after the header; each key is an id, the key's length, its type, the key and a NUL.
const std::size_t kKeyTableOffset = 0x10;
const std::size_t kKeyHeaderSize = 5;
const std::size_t kKeyLengthOffset = 2;
const char kKeyTableEnd[] = {'\xAA', '\xBB', '\xCC', '\xDD', '\xEE'};
// Each value names its key by where the key starts in the key table, then gives its length,
// its type and the value with a NUL.
const std::size_t kValueTableOffset = 0x10000;
const std::size_t kValueHeaderSize = 9;
const std::size_t kValueKeyOffset = 2;
const std::size_t kValueLengthOffset = 6;
const std::size_t kValueTypeOffset = 8;
const unsigned char kStringType = 2;

/** The big-endian number of 2 bytes at an offset; the caller checks the bounds. */
std::size_t ReadBigEndian16(const std::string &bytes, std::size_t offset)
{
    return static_cast<std::size_t>(static_cast<unsigned char>(bytes[offset])) << 8 |
           static_cast<unsigned char>(bytes[offset + 1]);
}

/** Whether every character is a lower-case hex digit, as in an account id. */
bool IsHexDigits(const std::string &text)
{
    return text.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// What looking for a key found, besides where the key starts.
const std::size_t kKeyAbsent = static_cast<std::size_t>(-1);
const std::size_t kKeyTableDamaged = static_cast<std::size_t>(-2);

/**
 * Where a key's entry starts, counted from the key table.
 * @return the offset; kKeyAbsent when the table ends without the key; kKeyTableDamaged when the
 * table runs off the file, so whether the key is there is not known
 */
std::size_t FindKey(const std::string &registry, const std::string &key)
{
    std::size_t at = kKeyTableOffset;
    while (at + kKeyHeaderSize <= registry.size())
    {
        if (registry.compare(at, sizeof kKeyTableEnd, kKeyTableEnd, sizeof kKeyTableEnd) == 0)
        {
            return kKeyAbsent;
        }
        const std::size_t length = ReadBigEndian16(registry, at + kKeyLengthOffset);
        if (length > registry.size() - at - kKeyHeaderSize)
        {
            return kKeyTableDamaged;
        }
        if (registry.compare(at + kKeyHeaderSize, length, key) == 0)
        {
            return at - kKeyTableOffset;
        }
        at += kKeyHeaderSize + length + 1;
    }
    return kKeyTableDamaged;
}
} // namespace

std::optional<std::string> FindAccountId(const std::string &registry, const std::string &user)
{
    const std::size_t key = FindKey(registry, "/setting/user/" + user + "/npaccount/accountid");
    if (key == kKeyAbsent)
    {
        return std::string(kAccountIdSize, '\0');
    }
    if (key == kKeyTableDamaged)
    {
        return std::nullopt;
    }
    // The values are searched byte by byte, as the table's layout is not fully known: an account
    // id is a string of 16 hex digits and a NUL, which a stray match is unlikely to be.
    bool emptyValue = false;
    for (std::size_t at = kValueTableOffset; at + kValueHeaderSize < registry.size(); ++at)
    {
        if (ReadBigEndian16(registry, at + kValueKeyOffset) != key ||
            static_cast<unsigned char>(registry[at + kValueTypeOffset]) != kStringType)
        {
            continue;
        }
        const std::size_t length = ReadBigEndian16(registry, at + kValueLengthOffset);
        const std::size_t value = at + kValueHeaderSize;
        if (length > registry.size() - value)
        {
            continue;
        }
        const std::string text = registry.substr(value, length);
        if (length == kAccountIdSize + 1 && text.back() == '\0' &&
            IsHexDigits(text.substr(0, kAccountIdSize)))
        {
            return text.substr(0, kAccountIdSize);
        }
        // A user who left PSN may keep the key with an empty value: just its NUL, or NULs as long
        // as an account id; other lengths would make a stray match too likely.
        const bool emptyLength = length == 1 || length == kAccountIdSize + 1;
        emptyValue =
            emptyValue || (emptyLength && text.find_first_not_of('\0') == std::string::npos);
    }
    if (emptyValue)
    {
        return std::string(kAccountIdSize, '\0');
    }
    return std::nullopt;
}
