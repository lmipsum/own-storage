// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/ParamPfd.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include <cctype>
#include <cstdint>

namespace
{
// The layout of a PARAM.PFD; every number in it is big-endian.
const std::size_t kPfdSize = 0x8000;
const char kMagic[] = {'\0', '\0', '\0', '\0', 'P', 'F', 'D', 'B'};
const std::size_t kVersionOffset = 0x08;
const std::uint64_t kVersionWithKeygen = 4;
const std::uint64_t kOldestVersion = 3;
// The header key is the IV of the signature block, which holds the bottom hash, the top hash
// and the hash key, encrypted.
const std::size_t kHeaderKeyOffset = 0x10;
const std::size_t kSignatureOffset = 0x20;
const std::size_t kSignatureSize = 0x40;
const std::size_t kBottomHashOffset = 0x20;
const std::size_t kTopHashOffset = 0x34;
const std::size_t kHashKeyOffset = 0x48;
// The tables: as many buckets as entry signatures, and a fixed number of entries.
const std::size_t kTablesOffset = 0x60;
const std::size_t kCapacityOffset = 0x60;
const std::size_t kReservedOffset = 0x68;
const std::size_t kBucketsOffset = 0x78;
const std::uint64_t kCapacity = 57;
const std::uint64_t kReserved = 114;
const std::size_t kEntriesOffset = kBucketsOffset + kCapacity * 8;
const std::size_t kEntrySize = 272;
const std::size_t kSignaturesOffset = kEntriesOffset + kReserved * kEntrySize;
// An entry: the next entry of its bucket, the file's name, then what its signature covers from
// its key on: the key, the file's four hashes, padding and the file's size.
const std::size_t kEntryNext = 0x00;
const std::size_t kEntryName = 0x08;
const std::size_t kEntryNameSize = 65;
const std::size_t kEntrySigned = 0x50;
const std::size_t kEntrySignedSize = 0xC0;
const std::size_t kEntryHashes = 0x90;
const std::size_t kEntryFileSize = 0x108;

const std::size_t kHashSize = 20;
const std::size_t kAesKeySize = 16;
// The four hashes of PARAM.SFO, each keyed differently.
enum SfoHash
{
    kFileHash,
    kConsoleHash,
    kDiscHash,
    kAuthenticationHash,
    kSfoHashes
};

const unsigned char kSysconManagerKey[] = {0xD4, 0x13, 0xB8, 0x96, 0x63, 0xE1, 0xFE, 0x9F,
                                           0x75, 0x14, 0x3D, 0x3B, 0xB4, 0x56, 0x52, 0x74};
const unsigned char kKeygenKey[] = {0x6B, 0x1A, 0xCE, 0xA2, 0x46, 0xB7, 0x45, 0xFD, 0x8F, 0x93,
                                    0x76, 0x3B, 0x92, 0x05, 0x94, 0xCD, 0x53, 0x48, 0x3B, 0x82};
const unsigned char kSavegameParamSfoKey[] = {0x0C, 0x08, 0x00, 0x0E, 0x09, 0x05, 0x04,
                                              0x04, 0x0D, 0x01, 0x0F, 0x00, 0x04, 0x06,
                                              0x02, 0x02, 0x09, 0x06, 0x0D, 0x03};
// The disc hash key of games started from the hard disk; most games' saves use it.
const unsigned char kFallbackDiscHashKey[] = {0xD1, 0xC1, 0xE1, 0x0B, 0x9C, 0x54, 0x7E, 0x68,
                                              0x9B, 0x80, 0x5D, 0xCD, 0x97, 0x10, 0xCE, 0x8D};
const unsigned char kAuthenticationId[] = {0x10, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03};

/** The big-endian number of 8 bytes at an offset; the caller checks the bounds. */
std::uint64_t ReadBigEndian(const std::string &bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    }
    return value;
}

/** Writes a big-endian number of 8 bytes at an offset; the caller checks the bounds. */
void WriteBigEndian(std::string &bytes, std::size_t offset, std::uint64_t value)
{
    for (std::size_t i = 8; i > 0; --i)
    {
        bytes[offset + i - 1] = static_cast<char>(value & 0xff);
        value >>= 8;
    }
}

/** HMAC-SHA1 of data with a key; empty when it cannot be made. */
std::string Hmac(const void *key, std::size_t keySize, const std::string &data)
{
    unsigned char digest[kHashSize];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),
                        static_cast<const unsigned char *>(key), keySize,
                        reinterpret_cast<const unsigned char *>(data.data()), data.size(),
                        digest) != 0)
    {
        return std::string();
    }
    return std::string(reinterpret_cast<const char *>(digest), kHashSize);
}

/**
 * Encrypts or decrypts the signature block in place with the syscon manager key.
 * @return false when it cannot be done
 */
bool CryptSignature(std::string &pfd, int mode)
{
    unsigned char iv[kAesKeySize];
    for (std::size_t i = 0; i < kAesKeySize; ++i)
    {
        iv[i] = static_cast<unsigned char>(pfd[kHeaderKeyOffset + i]);
    }
    unsigned char block[kSignatureSize];
    for (std::size_t i = 0; i < kSignatureSize; ++i)
    {
        block[i] = static_cast<unsigned char>(pfd[kSignatureOffset + i]);
    }
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    const int keyResult = mode == MBEDTLS_AES_ENCRYPT
                              ? mbedtls_aes_setkey_enc(&aes, kSysconManagerKey, 128)
                              : mbedtls_aes_setkey_dec(&aes, kSysconManagerKey, 128);
    const bool done =
        keyResult == 0 && mbedtls_aes_crypt_cbc(&aes, mode, kSignatureSize, iv, block, block) == 0;
    mbedtls_aes_free(&aes);
    if (done)
    {
        pfd.replace(kSignatureOffset, kSignatureSize, reinterpret_cast<const char *>(block),
                    kSignatureSize);
    }
    return done;
}

/** The bucket a file's entry is in: its name hashed the way the console does. */
std::uint64_t Bucket(const std::string &name)
{
    std::uint64_t hash = 0;
    for (const char c : name)
    {
        hash = hash * 31 + static_cast<unsigned char>(c);
    }
    return hash % kCapacity;
}

/** The signature of a bucket: of each entry in it, its name and the part from its key on. */
std::string BucketSignature(const std::string &pfd, const std::string &key, std::uint64_t bucket)
{
    std::string signedPart;
    std::uint64_t entry = ReadBigEndian(pfd, kBucketsOffset + bucket * 8);
    // A damaged chain could loop; no chain is longer than the entries there are.
    for (std::uint64_t steps = 0; entry < kReserved && steps < kReserved; ++steps)
    {
        const std::size_t at = kEntriesOffset + entry * kEntrySize;
        signedPart += pfd.substr(at + kEntryName, kEntryNameSize);
        signedPart += pfd.substr(at + kEntrySigned, kEntrySignedSize);
        entry = ReadBigEndian(pfd, at + kEntryNext);
    }
    return Hmac(key.data(), key.size(), signedPart);
}

/** The hash of the tables' header and buckets. */
std::string TopHash(const std::string &pfd, const std::string &key)
{
    return Hmac(key.data(), key.size(), pfd.substr(kTablesOffset, kEntriesOffset - kTablesOffset));
}

/** The hash of all bucket signatures. */
std::string BottomHash(const std::string &pfd, const std::string &key)
{
    return Hmac(key.data(), key.size(), pfd.substr(kSignaturesOffset, kCapacity * kHashSize));
}

/** Whether a decrypted PFD's buckets, top hash and bottom hash hold. */
bool SignaturesHold(const std::string &pfd, const std::string &key)
{
    for (std::uint64_t bucket = 0; bucket < kCapacity; ++bucket)
    {
        if (BucketSignature(pfd, key, bucket) !=
            pfd.substr(kSignaturesOffset + bucket * kHashSize, kHashSize))
        {
            return false;
        }
    }
    return TopHash(pfd, key) == pfd.substr(kTopHashOffset, kHashSize) &&
           BottomHash(pfd, key) == pfd.substr(kBottomHashOffset, kHashSize);
}

/**
 * Signs every bucket, then the tables, of a decrypted PFD.
 * @return false when a signature cannot be made
 */
bool Sign(std::string &pfd, const std::string &key)
{
    for (std::uint64_t bucket = 0; bucket < kCapacity; ++bucket)
    {
        const std::string signature = BucketSignature(pfd, key, bucket);
        if (signature.empty())
        {
            return false;
        }
        pfd.replace(kSignaturesOffset + bucket * kHashSize, kHashSize, signature);
    }
    const std::string bottom = BottomHash(pfd, key);
    if (bottom.empty())
    {
        return false;
    }
    pfd.replace(kBottomHashOffset, kHashSize, bottom);
    const std::string top = TopHash(pfd, key);
    if (top.empty())
    {
        return false;
    }
    pfd.replace(kTopHashOffset, kHashSize, top);
    return true;
}

/** Where PARAM.SFO's entry is; 0 when the PFD has none. */
std::size_t SfoEntry(const std::string &pfd)
{
    const std::string sfoName = "PARAM.SFO";
    std::uint64_t entry = ReadBigEndian(pfd, kBucketsOffset + Bucket(sfoName) * 8);
    for (std::uint64_t steps = 0; entry < kReserved && steps < kReserved; ++steps)
    {
        const std::size_t at = kEntriesOffset + entry * kEntrySize;
        const std::string name = pfd.substr(at + kEntryName, sfoName.size() + 1);
        bool same = name.size() == sfoName.size() + 1 && name.back() == '\0';
        for (std::size_t i = 0; same && i < sfoName.size(); ++i)
        {
            same = std::toupper(static_cast<unsigned char>(name[i])) == sfoName[i];
        }
        if (same)
        {
            return at;
        }
        entry = ReadBigEndian(pfd, at + kEntryNext);
    }
    return 0;
}

/** The four hashes of a PARAM.SFO for a console; empty when one cannot be made. */
std::string SfoHashes(const std::string &sfo, const std::string &idps)
{
    const std::string hashes[kSfoHashes] = {
        Hmac(kSavegameParamSfoKey, sizeof kSavegameParamSfoKey, sfo),
        Hmac(idps.data(), idps.size(), sfo),
        Hmac(kFallbackDiscHashKey, sizeof kFallbackDiscHashKey, sfo),
        Hmac(kAuthenticationId, sizeof kAuthenticationId, sfo)};
    std::string all;
    for (const std::string &hash : hashes)
    {
        if (hash.empty())
        {
            return std::string();
        }
        all += hash;
    }
    return all;
}
} // namespace

PfdResign ResignPfd(std::string &pfd, const std::string &signedSfo, const std::string &newSfo,
                    const std::string &idps)
{
    if (pfd.size() != kPfdSize || pfd.compare(0, sizeof kMagic, kMagic, sizeof kMagic) != 0 ||
        idps.size() != kIdpsSize || ReadBigEndian(pfd, kCapacityOffset) != kCapacity ||
        ReadBigEndian(pfd, kReservedOffset) != kReserved)
    {
        return PfdResign::Damaged;
    }
    const std::uint64_t version = ReadBigEndian(pfd, kVersionOffset);
    std::string decrypted = pfd;
    if (version < kOldestVersion || version > kVersionWithKeygen ||
        !CryptSignature(decrypted, MBEDTLS_AES_DECRYPT))
    {
        return PfdResign::Damaged;
    }
    const std::string hashKey = decrypted.substr(kHashKeyOffset, kHashSize);
    const std::string key =
        version == kVersionWithKeygen ? Hmac(kKeygenKey, sizeof kKeygenKey, hashKey) : hashKey;
    if (key.empty() || !SignaturesHold(decrypted, key))
    {
        return PfdResign::Damaged;
    }
    const std::size_t entry = SfoEntry(decrypted);
    if (entry == 0)
    {
        return PfdResign::NoSfoEntry;
    }
    const std::string stored = decrypted.substr(entry + kEntryHashes, kSfoHashes * kHashSize);
    // The console hash was made with the IDPS of the console the save came from, so it cannot be
    // checked; the others tell whether the keys are right.
    const std::string expected = SfoHashes(signedSfo, idps);
    const auto same = [&](SfoHash hash)
    {
        return expected.compare(hash * kHashSize, kHashSize, stored, hash * kHashSize, kHashSize) ==
               0;
    };
    if (expected.empty() || ReadBigEndian(decrypted, entry + kEntryFileSize) != signedSfo.size() ||
        !same(kFileHash) || !same(kAuthenticationHash))
    {
        return PfdResign::Damaged;
    }
    if (!same(kDiscHash))
    {
        return PfdResign::UnknownDiscKey;
    }
    const std::string hashes = SfoHashes(newSfo, idps);
    if (hashes.empty())
    {
        return PfdResign::Damaged;
    }
    decrypted.replace(entry + kEntryHashes, hashes.size(), hashes);
    WriteBigEndian(decrypted, entry + kEntryFileSize, newSfo.size());
    if (!Sign(decrypted, key) || !CryptSignature(decrypted, MBEDTLS_AES_ENCRYPT))
    {
        return PfdResign::Damaged;
    }
    pfd = decrypted;
    return PfdResign::Resigned;
}
