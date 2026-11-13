// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * A digest of file content that stands in for MD5 in the tests: the sync only compares
 * digests, and the test cloud makes them the same way.
 */
std::string ContentDigest(const std::string &content);
