// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "drive/GoogleDrive.h"

/**
 * The lower progress bar of the sync dialog, as GoogleDrive moves a file by itself. For the sync
 * thread only.
 * @return what GoogleDrive calls as the file moves
 */
TransferDisplay MakeTransferBar();
