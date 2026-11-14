// SPDX-License-Identifier: GPL-3.0-or-later
#include "SaveFiles.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include <cstddef>

namespace
{
/** Appends a little-endian number of a byte count. */
void PutLittleEndian(std::string &bytes, std::uint32_t value, int count)
{
    for (int i = 0; i < count; ++i)
    {
        bytes += static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

/** Writes a big-endian number of 8 bytes at an offset. */
void PutBigEndian(std::string &bytes, std::size_t offset, std::uint64_t value)
{
    for (int i = 7; i >= 0; --i)
    {
        bytes[offset + i] = static_cast<char>(value & 0xff);
        value >>= 8;
    }
}

/** One index entry of a PARAM.SFO. */
void PutEntry(std::string &sfo, std::uint32_t keyOffset, std::uint32_t format, std::uint32_t length,
              std::uint32_t dataOffset)
{
    PutLittleEndian(sfo, keyOffset, 2);
    PutLittleEndian(sfo, format, 2);
    PutLittleEndian(sfo, length, 4);
    PutLittleEndian(sfo, length, 4);
    PutLittleEndian(sfo, dataOffset, 4);
}

std::string Bytes(const char *hex)
{
    std::string bytes;
    for (std::size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2)
    {
        bytes += static_cast<char>(std::stoi(std::string(hex + i, 2), nullptr, 16));
    }
    return bytes;
}

std::string Hmac(const std::string &key, const std::string &data)
{
    unsigned char digest[20];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),
                    reinterpret_cast<const unsigned char *>(key.data()), key.size(),
                    reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest);
    return std::string(reinterpret_cast<const char *>(digest), sizeof digest);
}

std::uint64_t ReadBigEndian(const std::string &bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    }
    return value;
}

const std::size_t kCapacity = 57;
const std::size_t kReserved = 114;
const std::size_t kBuckets = 0x78;
const std::size_t kEntries = kBuckets + kCapacity * 8;
const std::size_t kEntrySize = 272;
const std::size_t kSignatures = kEntries + kReserved * kEntrySize;

std::size_t BucketOf(const std::string &name)
{
    std::uint64_t hash = 0;
    for (const char c : name)
    {
        hash = hash * 31 + static_cast<unsigned char>(c);
    }
    return hash % kCapacity;
}

/** Adds a file's entry at the head of its bucket's chain. */
void PutPfdEntry(std::string &pfd, std::size_t index, const std::string &name,
                 const std::string &hashes, std::uint64_t size)
{
    const std::size_t at = kEntries + index * kEntrySize;
    const std::size_t bucket = kBuckets + BucketOf(name) * 8;
    PutBigEndian(pfd, at, ReadBigEndian(pfd, bucket));
    PutBigEndian(pfd, bucket, index);
    pfd.replace(at + 8, name.size(), name);
    for (std::size_t i = 0; i < 64; ++i)
    {
        pfd[at + 0x50 + i] = static_cast<char>(index * 7 + i);
    }
    pfd.replace(at + 0x90, hashes.size(), hashes);
    PutBigEndian(pfd, at + 0x108, size);
}
} // namespace

SaveOwner TestOwner(std::uint32_t userId, char psidByte, const std::string &accountId)
{
    std::string account = accountId;
    account.resize(kAccountIdSize, '\0');
    return SaveOwner{userId, std::string(kPsidSize, psidByte), account};
}

std::string MakeSfo(const std::string &title, const SaveOwner &owner)
{
    // The keys sorted A to Z, as the console sorts them; their table is 24 bytes, 4-aligned.
    const std::string keys("ACCOUNT_ID\0PARAMS\0TITLE\0", 24);
    const std::uint32_t paramsSize = 0x400;
    const std::uint32_t keyTable = 20 + 3 * 16;
    std::string sfo("\0PSF", 4);
    PutLittleEndian(sfo, 0x0101, 4);
    PutLittleEndian(sfo, keyTable, 4);
    PutLittleEndian(sfo, keyTable + keys.size(), 4);
    PutLittleEndian(sfo, 3, 4);
    PutEntry(sfo, 0, 0x0004, kAccountIdSize, 0);
    PutEntry(sfo, 11, 0x0004, paramsSize, kAccountIdSize);
    PutEntry(sfo, 18, 0x0204, title.size() + 1, kAccountIdSize + paramsSize);
    std::string params(paramsSize, '\0');
    std::string userId;
    PutLittleEndian(userId, owner.userId, 4);
    params.replace(0x18, 4, userId);
    params.replace(0x1C, kPsidSize, owner.psid);
    params.replace(0x2C, 4, userId);
    params.replace(0x30, kAccountIdSize, owner.accountId);
    return sfo + keys + owner.accountId + params + title + '\0';
}

std::string MakeSfo(const std::string &title)
{
    return MakeSfo(title, TestOwner(1, 'a', "0123456789abcdef"));
}

std::string FallbackDiscHashKey()
{
    return Bytes("D1C1E10B9C547E689B805DCD9710CE8D");
}

std::string MakePfd(const std::string &sfo, const std::string &idps, const std::string &discHashKey,
                    int version)
{
    std::string pfd(0x8000, '\0');
    pfd.replace(4, 4, "PFDB");
    PutBigEndian(pfd, 0x08, version);
    for (std::size_t i = 0; i < 16; ++i)
    {
        pfd[0x10 + i] = static_cast<char>(0x40 + i);
    }
    PutBigEndian(pfd, 0x60, kCapacity);
    PutBigEndian(pfd, 0x68, kReserved);
    PutBigEndian(pfd, 0x70, 2);
    for (std::size_t bucket = 0; bucket < kCapacity; ++bucket)
    {
        PutBigEndian(pfd, kBuckets + bucket * 8, kReserved);
    }
    const std::string hashes = Hmac(Bytes("0C08000E090504040D010F000406020209060D03"), sfo) +
                               Hmac(idps, sfo) + Hmac(discHashKey, sfo) +
                               Hmac(Bytes("1010000001000003"), sfo);
    PutPfdEntry(pfd, 0, "PARAM.SFO", hashes, sfo.size());
    PutPfdEntry(pfd, 1, "DATA.BIN", std::string(20, '\x5A'), 4096);

    std::string hashKey;
    for (std::size_t i = 0; i < 20; ++i)
    {
        hashKey += static_cast<char>(0x20 + i);
    }
    pfd.replace(0x48, 20, hashKey);
    const std::string key =
        version == 4 ? Hmac(Bytes("6B1ACEA246B745FD8F93763B920594CD53483B82"), hashKey) : hashKey;
    for (std::size_t bucket = 0; bucket < kCapacity; ++bucket)
    {
        std::string signedPart;
        for (std::uint64_t entry = ReadBigEndian(pfd, kBuckets + bucket * 8); entry < kReserved;
             entry = ReadBigEndian(pfd, kEntries + entry * kEntrySize))
        {
            const std::size_t at = kEntries + entry * kEntrySize;
            signedPart += pfd.substr(at + 8, 65) + pfd.substr(at + 0x50, 0xC0);
        }
        pfd.replace(kSignatures + bucket * 20, 20, Hmac(key, signedPart));
    }
    pfd.replace(0x20, 20, Hmac(key, pfd.substr(kSignatures, kCapacity * 20)));
    pfd.replace(0x34, 20, Hmac(key, pfd.substr(0x60, kEntries - 0x60)));

    unsigned char iv[16];
    unsigned char block[64];
    for (std::size_t i = 0; i < 16; ++i)
    {
        iv[i] = static_cast<unsigned char>(pfd[0x10 + i]);
    }
    for (std::size_t i = 0; i < 64; ++i)
    {
        block[i] = static_cast<unsigned char>(pfd[0x20 + i]);
    }
    const std::string sysconKey = Bytes("D413B89663E1FE9F75143D3BB4565274");
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, reinterpret_cast<const unsigned char *>(sysconKey.data()), 128);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, sizeof block, iv, block, block);
    mbedtls_aes_free(&aes);
    pfd.replace(0x20, 64, reinterpret_cast<const char *>(block), 64);
    return pfd;
}
