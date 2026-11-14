// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Tar.h"

#include <algorithm>
#include <stdio.h>

namespace
{
// Where the ustar header keeps each field, and how long it is.
const std::size_t kNameOffset = 0;
const std::size_t kNameLength = 100;
const std::size_t kModeOffset = 100;
const std::size_t kOwnerOffset = 108;
const std::size_t kGroupOffset = 116;
const std::size_t kIdLength = 8;
const std::size_t kSizeOffset = 124;
const std::size_t kSizeLength = 12;
const std::size_t kTimeOffset = 136;
const std::size_t kTimeLength = 12;
const std::size_t kChecksumOffset = 148;
const std::size_t kChecksumLength = 8;
const std::size_t kTypeOffset = 156;
const std::size_t kMagicOffset = 257;
const char kMagic[] = "ustar";
const char kVersion[] = "00";
const char kRegularFile = '0';
// Read and write for the owner, read for everyone.
const unsigned int kFileMode = 0644;

/** Writes a number in octal, zero-padded, ending in a NUL, into a header field. */
void PutOctal(std::string &header, std::size_t offset, std::size_t length, unsigned long long value)
{
    char text[24];
    // The console's C++ library offers snprintf only outside std.
    snprintf(text, sizeof text, "%0*llo", static_cast<int>(length - 1), value);
    header.replace(offset, length - 1, text, length - 1);
}

/** Reads an octal header field; false when it holds anything else. */
bool GetOctal(const std::string &block, std::size_t offset, std::size_t length,
              unsigned long long &value)
{
    value = 0;
    for (std::size_t i = offset; i < offset + length && block[i] != '\0' && block[i] != ' '; ++i)
    {
        if (block[i] < '0' || block[i] > '7')
        {
            return false;
        }
        value = value * 8 + static_cast<unsigned long long>(block[i] - '0');
    }
    return true;
}

/** The header's checksum: the sum of its bytes, with the checksum field counted as spaces. */
unsigned long long Checksum(const std::string &header)
{
    unsigned long long sum = 0;
    for (std::size_t i = 0; i < kTarBlock; ++i)
    {
        const bool inField = i >= kChecksumOffset && i < kChecksumOffset + kChecksumLength;
        sum += inField ? ' ' : static_cast<unsigned char>(header[i]);
    }
    return sum;
}

} // namespace

std::string TarHeader(const std::string &name, unsigned long long size)
{
    std::string header(kTarBlock, '\0');
    header.replace(kNameOffset, name.size(), name);
    PutOctal(header, kModeOffset, kIdLength, kFileMode);
    PutOctal(header, kOwnerOffset, kIdLength, 0);
    PutOctal(header, kGroupOffset, kIdLength, 0);
    PutOctal(header, kSizeOffset, kSizeLength, size);
    PutOctal(header, kTimeOffset, kTimeLength, 0);
    header[kTypeOffset] = kRegularFile;
    header.replace(kMagicOffset, sizeof kMagic, kMagic, sizeof kMagic);
    header.replace(kMagicOffset + sizeof kMagic, sizeof kVersion - 1, kVersion,
                   sizeof kVersion - 1);
    // Six octal digits, a NUL and a space, as tar writes it.
    char checksum[kChecksumLength + 1];
    snprintf(checksum, sizeof checksum, "%06llo", Checksum(header));
    header.replace(kChecksumOffset, 6, checksum, 6);
    header[kChecksumOffset + 6] = '\0';
    header[kChecksumOffset + 7] = ' ';
    return header;
}

std::size_t TarPadding(unsigned long long size)
{
    return static_cast<std::size_t>((kTarBlock - size % kTarBlock) % kTarBlock);
}

std::string TarEnd()
{
    return std::string(2 * kTarBlock, '\0');
}

TarBlock ReadTarHeader(const std::string &block, std::string &name, unsigned long long &size)
{
    if (block.size() != kTarBlock)
    {
        return TarBlock::Bad;
    }
    if (block == std::string(kTarBlock, '\0'))
    {
        return TarBlock::End;
    }
    unsigned long long checksum;
    if (block.compare(kMagicOffset, sizeof kMagic - 1, kMagic) != 0 ||
        block[kTypeOffset] != kRegularFile || !GetOctal(block, kSizeOffset, kSizeLength, size) ||
        !GetOctal(block, kChecksumOffset, kChecksumLength, checksum) || checksum != Checksum(block))
    {
        return TarBlock::Bad;
    }
    name = block.substr(kNameOffset, std::min(block.find('\0', kNameOffset), kNameLength));
    return IsArchivableName(name) ? TarBlock::File : TarBlock::Bad;
}

bool IsArchivableName(const std::string &name)
{
    return !name.empty() && name.size() < kNameLength && name.find('/') == std::string::npos &&
           name != "." && name != "..";
}
