// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Resign.h"

#include "sync/ParamPfd.h"

#include <algorithm>

namespace
{
/** A file of the save by name; null when the save has none. */
TarFile *FindFile(std::vector<TarFile> &files, const char *name)
{
    const auto file = std::find_if(files.begin(), files.end(),
                                   [name](const TarFile &each) { return each.name == name; });
    return file == files.end() ? nullptr : &*file;
}
} // namespace

ResignResult ResignSave(std::vector<TarFile> &files, const SaveTarget &target)
{
    TarFile *sfo = FindFile(files, paramSfoName);
    SaveOwner owner;
    if (sfo == nullptr || !ReadSfoOwner(sfo->content, owner) || owner == target.owner)
    {
        return ResignResult::Unchanged;
    }
    std::string newSfo = sfo->content;
    if (!WriteSfoOwner(newSfo, target.owner))
    {
        return ResignResult::Refused;
    }
    TarFile *pfd = FindFile(files, paramPfdName);
    if (pfd != nullptr)
    {
        std::string newPfd = pfd->content;
        const PfdResign signedAgain = ResignPfd(newPfd, sfo->content, newSfo, target.idps);
        if (signedAgain != PfdResign::Resigned && signedAgain != PfdResign::NoSfoEntry)
        {
            return ResignResult::Refused;
        }
        pfd->content = newPfd;
    }
    sfo->content = newSfo;
    return ResignResult::Resigned;
}
