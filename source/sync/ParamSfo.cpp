// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/ParamSfo.h"

#include <cstddef>

namespace
{
// The layout of a PARAM.SFO; every number in it is little-endian, whatever the console is.
const char kMagic[] = {'\0', 'P', 'S', 'F'};
const std::size_t kKeyTableOffset = 8;
const std::size_t kDataTableOffset = 12;
const std::size_t kEntryCountOffset = 16;
const std::size_t kHeaderSize = 20;
// Each entry of the index that follows the header.
const std::size_t kEntrySize = 16;
const std::size_t kEntryKeyOffset = 0;
const std::size_t kEntryFormat = 2;
const std::size_t kEntryLength = 4;
const std::size_t kEntryDataOffset = 12;
// A text value whose length counts the closing NUL, and one without a NUL.
const std::uint32_t kUtf8Format = 0x0204;
const std::uint32_t kRawUtf8Format = 0x0004;
const char kTitleKey[] = "TITLE";
// The save's owner: in PARAMS, the user's number twice, the console's PSID and the PSN account
// id; the account id once more under ACCOUNT_ID.
const char kParamsKey[] = "PARAMS";
const char kAccountIdKey[] = "ACCOUNT_ID";
const std::size_t kParamsUserId = 0x18;
const std::size_t kParamsPsid = 0x1C;
const std::size_t kParamsUserIdAgain = 0x2C;
const std::size_t kParamsAccountId = 0x30;
const std::size_t kParamsOwnerEnd = kParamsAccountId + kAccountIdSize;

/** The little-endian number of a byte count at an offset; the caller checks the bounds. */
std::uint32_t ReadLittleEndian(const std::string &bytes, std::size_t offset, std::size_t count)
{
    std::uint32_t value = 0;
    for (std::size_t i = count; i > 0; --i)
    {
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i - 1]);
    }
    return value;
}

/** Writes a little-endian number of four bytes at an offset; the caller checks the bounds. */
void WriteLittleEndian(std::string &bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

/** Where a value is in a PARAM.SFO. */
struct SfoValue
{
    std::size_t offset;
    std::size_t length;
    std::uint32_t format;
};

/**
 * Finds a key's value in a PARAM.SFO.
 * @param sfo the whole file
 * @param key the key
 * @param keySize the key's bytes with its closing NUL
 * @param value receives where its value is, which lies inside the file
 * @return false when the file is not a PARAM.SFO or has no such key
 */
bool FindValue(const std::string &sfo, const char *key, std::size_t keySize, SfoValue &value)
{
    if (sfo.size() < kHeaderSize || sfo.compare(0, sizeof kMagic, kMagic, sizeof kMagic) != 0)
    {
        return false;
    }
    const std::size_t keyTable = ReadLittleEndian(sfo, kKeyTableOffset, 4);
    const std::size_t dataTable = ReadLittleEndian(sfo, kDataTableOffset, 4);
    const std::size_t entries = ReadLittleEndian(sfo, kEntryCountOffset, 4);
    if (entries > (sfo.size() - kHeaderSize) / kEntrySize || keyTable > sfo.size() ||
        dataTable > sfo.size())
    {
        return false;
    }
    for (std::size_t entry = 0; entry < entries; ++entry)
    {
        const std::size_t at = kHeaderSize + entry * kEntrySize;
        const std::size_t keyAt = keyTable + ReadLittleEndian(sfo, at + kEntryKeyOffset, 2);
        // The key's closing NUL is compared too, so a longer key does not match.
        if (keyAt >= sfo.size() || sfo.compare(keyAt, keySize, key, keySize) != 0)
        {
            continue;
        }
        const std::size_t length = ReadLittleEndian(sfo, at + kEntryLength, 4);
        const std::size_t offset = ReadLittleEndian(sfo, at + kEntryDataOffset, 4);
        // Compared by subtraction: on the console a sum of offsets could wrap around.
        if (offset > sfo.size() - dataTable || length > sfo.size() - dataTable - offset)
        {
            return false;
        }
        value = SfoValue{dataTable + offset, length, ReadLittleEndian(sfo, at + kEntryFormat, 2)};
        return true;
    }
    return false;
}

/** Finds PARAMS and ACCOUNT_ID, both long enough to hold the owner. */
bool FindOwner(const std::string &sfo, SfoValue &params, SfoValue &accountId)
{
    return FindValue(sfo, kParamsKey, sizeof kParamsKey, params) &&
           params.format == kRawUtf8Format && params.length >= kParamsOwnerEnd &&
           FindValue(sfo, kAccountIdKey, sizeof kAccountIdKey, accountId) &&
           accountId.format == kRawUtf8Format && accountId.length >= kAccountIdSize;
}
} // namespace

bool operator==(const SaveOwner &a, const SaveOwner &b)
{
    return a.userId == b.userId && a.psid == b.psid && a.accountId == b.accountId;
}

std::string ReadSfoTitle(const std::string &sfo)
{
    SfoValue title;
    if (!FindValue(sfo, kTitleKey, sizeof kTitleKey, title) || title.format != kUtf8Format)
    {
        return std::string();
    }
    const std::string value = sfo.substr(title.offset, title.length);
    return value.substr(0, value.find('\0'));
}

bool ReadSfoOwner(const std::string &sfo, SaveOwner &owner)
{
    SfoValue params;
    SfoValue accountId;
    if (!FindOwner(sfo, params, accountId))
    {
        return false;
    }
    owner = SaveOwner{ReadLittleEndian(sfo, params.offset + kParamsUserId, 4),
                      sfo.substr(params.offset + kParamsPsid, kPsidSize),
                      sfo.substr(params.offset + kParamsAccountId, kAccountIdSize)};
    return true;
}

bool WriteSfoOwner(std::string &sfo, const SaveOwner &owner)
{
    SfoValue params;
    SfoValue accountId;
    if (owner.psid.size() != kPsidSize || owner.accountId.size() != kAccountIdSize ||
        !FindOwner(sfo, params, accountId))
    {
        return false;
    }
    WriteLittleEndian(sfo, params.offset + kParamsUserId, owner.userId);
    sfo.replace(params.offset + kParamsPsid, kPsidSize, owner.psid);
    WriteLittleEndian(sfo, params.offset + kParamsUserIdAgain, owner.userId);
    sfo.replace(params.offset + kParamsAccountId, kAccountIdSize, owner.accountId);
    sfo.replace(accountId.offset, kAccountIdSize, owner.accountId);
    return true;
}
