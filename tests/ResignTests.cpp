// SPDX-License-Identifier: GPL-3.0-or-later
#include "Check.h"
#include "SaveFiles.h"

#include "app/XRegistry.h"
#include "sync/ParamPfd.h"
#include "sync/ParamSfo.h"
#include "sync/Resign.h"

#include <string>
#include <vector>

namespace
{
const SaveOwner kThere = TestOwner(1, 'a', "0123456789abcdef");
const SaveOwner kHere = TestOwner(2, 'b', "");
const std::string kIdpsThere(kIdpsSize, '\x11');
const std::string kIdpsHere(kIdpsSize, '\x22');

/** A save of another console's user, signed with the fallback disc hash key. */
std::vector<TarFile> SaveFromThere(int pfdVersion)
{
    const std::string sfo = MakeSfo("Game One", kThere);
    return {{"DATA.BIN", "data"},
            {paramPfdName, MakePfd(sfo, kIdpsThere, FallbackDiscHashKey(), pfdVersion)},
            {paramSfoName, sfo}};
}

/** Appends a big-endian number of 2 bytes. */
void PutBigEndian16(std::string &bytes, std::size_t value)
{
    bytes += static_cast<char>(value >> 8);
    bytes += static_cast<char>(value & 0xff);
}
} // namespace

TEST(OwnerIsReadAndWrittenInParamSfo)
{
    std::string sfo = MakeSfo("Game One", kThere);
    const std::size_t size = sfo.size();
    SaveOwner owner;
    CHECK(ReadSfoOwner(sfo, owner) && owner == kThere);
    CHECK(WriteSfoOwner(sfo, kHere));
    CHECK(ReadSfoOwner(sfo, owner) && owner == kHere);
    CHECK_EQUAL(size, sfo.size());
    CHECK_EQUAL(std::string("Game One"), ReadSfoTitle(sfo));
    CHECK_EQUAL(MakeSfo("Game One", kHere), sfo);
}

TEST(PfdIsSignedAnewForAnotherConsole)
{
    for (const int version : {3, 4})
    {
        const std::string sfoThere = MakeSfo("Game One", kThere);
        const std::string sfoHere = MakeSfo("Game One", kHere);
        std::string pfd = MakePfd(sfoThere, kIdpsThere, FallbackDiscHashKey(), version);
        CHECK(ResignPfd(pfd, sfoThere, sfoHere, kIdpsHere) == PfdResign::Resigned);
        // Signed anew, it is the PFD this console would have made for the save.
        CHECK(pfd == MakePfd(sfoHere, kIdpsHere, FallbackDiscHashKey(), version));
    }
}

TEST(PfdOfAGameWithItsOwnKeyOrDamagedIsLeftAlone)
{
    const std::string sfoThere = MakeSfo("Game One", kThere);
    const std::string sfoHere = MakeSfo("Game One", kHere);
    const std::string ownKey(16, '\x77');
    std::string pfd = MakePfd(sfoThere, kIdpsThere, ownKey, 3);
    const std::string original = pfd;
    CHECK(ResignPfd(pfd, sfoThere, sfoHere, kIdpsHere) == PfdResign::UnknownDiscKey);
    CHECK(pfd == original);

    pfd = MakePfd(sfoThere, kIdpsThere, FallbackDiscHashKey(), 3);
    pfd[0x7B60] ^= 1;
    const std::string damaged = pfd;
    CHECK(ResignPfd(pfd, sfoThere, sfoHere, kIdpsHere) == PfdResign::Damaged);
    CHECK(pfd == damaged);
    // Another PARAM.SFO than the one it signed is not taken for it.
    pfd = MakePfd(sfoThere, kIdpsThere, FallbackDiscHashKey(), 3);
    CHECK(ResignPfd(pfd, sfoHere, sfoHere, kIdpsHere) == PfdResign::Damaged);
}

TEST(SaveOfAnotherUserIsMadeThisUsers)
{
    std::vector<TarFile> files = SaveFromThere(3);
    CHECK(ResignSave(files, SaveTarget{kHere, kIdpsHere}) == ResignResult::Resigned);
    SaveOwner owner;
    CHECK(ReadSfoOwner(files[2].content, owner) && owner == kHere);
    CHECK(files[1].content == MakePfd(files[2].content, kIdpsHere, FallbackDiscHashKey(), 3));
    CHECK_EQUAL(std::string("data"), files[0].content);
    // Now it is this user's, it stays as it is.
    const std::vector<TarFile> resigned = files;
    CHECK(ResignSave(files, SaveTarget{kHere, kIdpsHere}) == ResignResult::Unchanged);
    CHECK(files[1].content == resigned[1].content && files[2].content == resigned[2].content);
}

TEST(SaveThatCannotBeMadeThisUsersStaysWhole)
{
    const std::string sfo = MakeSfo("Game One", kThere);
    std::vector<TarFile> files = {
        {paramPfdName, MakePfd(sfo, kIdpsThere, std::string(16, '\x77'), 3)}, {paramSfoName, sfo}};
    const std::vector<TarFile> original = files;
    CHECK(ResignSave(files, SaveTarget{kHere, kIdpsHere}) == ResignResult::Refused);
    CHECK(files[0].content == original[0].content && files[1].content == original[1].content);
    std::vector<TarFile> withoutSfo = {{"DATA.BIN", "data"}};
    CHECK(ResignSave(withoutSfo, SaveTarget{kHere, kIdpsHere}) == ResignResult::Unchanged);
}

TEST(AccountIdIsFoundInTheRegistry)
{
    // The header, then a key that only looks alike, the key itself and the end of the keys.
    std::string registry("\xBC\xAD\xAD\xBC\x00\x00\x00\x90\x00\x00\x00\x02\xBC\xAD\xAD\xBC", 16);
    const std::string other = "/setting/user/00000002/npaccount/accountid";
    const std::string key = "/setting/user/00000001/npaccount/accountid";
    PutBigEndian16(registry, 1);
    PutBigEndian16(registry, other.size());
    registry += std::string(1, '\x02') + other + '\0';
    const std::size_t keyOffset = registry.size() - 0x10;
    PutBigEndian16(registry, 2);
    PutBigEndian16(registry, key.size());
    registry += std::string(1, '\x02') + key + '\0';
    registry += std::string("\xAA\xBB\xCC\xDD\xEE\x00\x00", 7);
    registry.resize(0x10000, '\0');
    PutBigEndian16(registry, 0);
    PutBigEndian16(registry, keyOffset);
    PutBigEndian16(registry, 0);
    PutBigEndian16(registry, 17);
    registry += std::string(1, '\x02') + "fedcba9876543210" + '\0';
    registry.resize(0x40000, '\0');
    CHECK(FindAccountId(registry, "00000001") == std::string("fedcba9876543210"));
    // A user without the key has no PSN account; one whose key has no value is not taken for that.
    CHECK(FindAccountId(registry, "00000003") == std::string(kAccountIdSize, '\0'));
    CHECK(!FindAccountId(registry, "00000002"));
    // A value that only looks like an account id by its length is not taken for one.
    registry.replace(0x10000 + 9, kAccountIdSize, "GHIJKLMNOPQRSTUV");
    CHECK(!FindAccountId(registry, "00000001"));
}

TEST(EmptyAccountIdMeansNoAccount)
{
    std::string registry("\xBC\xAD\xAD\xBC\x00\x00\x00\x90\x00\x00\x00\x02\xBC\xAD\xAD\xBC", 16);
    const std::string key = "/setting/user/00000001/npaccount/accountid";
    PutBigEndian16(registry, 1);
    PutBigEndian16(registry, key.size());
    registry += std::string(1, '\x02') + key + '\0';
    registry += std::string("\xAA\xBB\xCC\xDD\xEE\x00\x00", 7);
    registry.resize(0x10000, '\0');
    PutBigEndian16(registry, 0);
    PutBigEndian16(registry, 0);
    PutBigEndian16(registry, 0);
    PutBigEndian16(registry, 17);
    registry += std::string(1, '\x02') + std::string(17, '\0');
    registry.resize(0x40000, '\x01');
    CHECK(FindAccountId(registry, "00000001") == std::string(kAccountIdSize, '\0'));
}

TEST(DamagedRegistryIsNotTakenForOneWithoutAnAccount)
{
    // Keys without the end of the table, the last one claiming more bytes than there are.
    std::string registry(16, '\0');
    PutBigEndian16(registry, 1);
    PutBigEndian16(registry, 0xFFFF);
    registry += "\x02/setting/user/00000001/npaccount/accountid";
    CHECK(!FindAccountId(registry, "00000001"));
    CHECK(!FindAccountId(std::string(), "00000001"));
}
