// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/SaveRegisterRequest.h"

#include "app/DebugLog.h"

SaveRegisterRequest::SaveRegisterRequest(LocalStorage &storage, const std::string &requestFile)
    : m_storage(storage), m_requestFile(requestFile)
{
}

void SaveRegisterRequest::Register(const std::string &folder)
{
    // Added to, not replaced: one run may download many saves before the plugin takes the file.
    if (!m_storage.Append(m_requestFile, folder + "\n"))
    {
        debugPrintf("SaveRegisterRequest::Register: %s cannot be written\n", m_requestFile.c_str());
    }
}
