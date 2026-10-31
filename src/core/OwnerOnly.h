#pragma once

// Whether a file or folder is closed to every user but its owner, for what
// Speecher keeps that other users must not read or take over.

#include <QtGlobal>

#ifdef Q_OS_UNIX
#include <QFile>
#include <QFileInfo>

#include <unistd.h>

namespace speecher {

// The permissions that let anyone but the owner in.
inline constexpr QFile::Permissions groupAndOthers = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
    | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;

// Whether path exists, belongs to this user and gives no one else any
// permission, as it reads now.
inline bool isOwnerOnly(const QString &path)
{
    const QFileInfo now(path);
    return now.exists() && now.ownerId() == getuid() && !(now.permissions() & groupAndOthers);
}

} // namespace speecher
#endif
