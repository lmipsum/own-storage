// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/ParamSfo.h"

#include <cstddef>
#include <cstdint>

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
// A text value; its length counts the closing NUL.
const std::uint32_t kUtf8Format = 0x0204;
const char kTitleKey[] = "TITLE";

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
} // namespace

std::string ReadSfoTitle(const std::string &sfo)
{
    if (sfo.size() < kHeaderSize || sfo.compare(0, sizeof kMagic, kMagic, sizeof kMagic) != 0)
    {
        return std::string();
    }
    const std::size_t keyTable = ReadLittleEndian(sfo, kKeyTableOffset, 4);
    const std::size_t dataTable = ReadLittleEndian(sfo, kDataTableOffset, 4);
    const std::size_t entries = ReadLittleEndian(sfo, kEntryCountOffset, 4);
    if (entries > (sfo.size() - kHeaderSize) / kEntrySize || keyTable > sfo.size() ||
        dataTable > sfo.size())
    {
        return std::string();
    }
    for (std::size_t entry = 0; entry < entries; ++entry)
    {
        const std::size_t at = kHeaderSize + entry * kEntrySize;
        const std::size_t key = keyTable + ReadLittleEndian(sfo, at + kEntryKeyOffset, 2);
        if (key >= sfo.size() ||
            sfo.compare(key, sizeof kTitleKey, kTitleKey, sizeof kTitleKey) != 0)
        {
            continue;
        }
        const std::size_t length = ReadLittleEndian(sfo, at + kEntryLength, 4);
        const std::size_t offset = ReadLittleEndian(sfo, at + kEntryDataOffset, 4);
        // Compared by subtraction: on the console a sum of offsets could wrap around.
        if (ReadLittleEndian(sfo, at + kEntryFormat, 2) != kUtf8Format ||
            offset > sfo.size() - dataTable || length > sfo.size() - dataTable - offset)
        {
            return std::string();
        }
        const std::string value = sfo.substr(dataTable + offset, length);
        return value.substr(0, value.find('\0'));
    }
    return std::string();
}
