// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

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

/**
 * Packs a save into one uncompressed ustar archive. The same files always give the same bytes:
 * they go in sorted by name, with fixed owner, mode and time, so two archives of one save have
 * the same MD5.
 * @param files the save's files; every name archivable
 * @return the archive
 */
std::string MakeTar(std::vector<TarFile> files);

/**
 * Unpacks an archive that MakeTar made.
 * @param archive the archive
 * @param files receives the files
 * @return false when it is not such an archive, or a name would leave the save folder
 */
bool ReadTar(const std::string &archive, std::vector<TarFile> &files);
