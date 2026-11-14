// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A save's archive is an uncompressed ustar archive. Its writer puts the files in sorted by name,
// and every file gets the same owner, mode and time, so two archives of one save have the same
// MD5.

#include <cstddef>
#include <string>

/** The size of an archive's blocks: a header is one, and a file's content fills whole ones. */
const std::size_t kTarBlock = 512;

/** One file of a save, as an archive holds it. */
struct TarFile
{
    /** The file's name in the save folder; saves have no subfolders. */
    std::string name;
    std::string content;
};

/**
 * Whether a name can go in an archive and stay in the save folder: not empty, shorter than 100
 * bytes, without a slash, and not "." or "..".
 */
bool IsArchivableName(const std::string &name);

/** What a header block of an archive is. */
enum class TarBlock
{
    /** A file's header. */
    File,
    /** The zero block that ends the archive. */
    End,
    /** Anything else, which a save's archive never holds. */
    Bad
};

/**
 * The header block of a file, with the fixed owner, mode and time every file gets.
 * @param name the file's name; archivable
 * @param size the content's size in bytes
 * @return the block
 */
std::string TarHeader(const std::string &name, unsigned long long size);

/** The zero bytes that fill a file's content of the given size up to a whole block. */
std::size_t TarPadding(unsigned long long size);

/** The two zero blocks that end an archive. */
std::string TarEnd();

/**
 * Reads a header block.
 * @param block the block, kTarBlock bytes
 * @param name receives the file's name, when the block names a file
 * @param size receives the content's size in bytes, when the block names a file
 * @return what the block is; Bad too for a name that would leave the save folder
 */
TarBlock ReadTarHeader(const std::string &block, std::string &name, unsigned long long &size);
