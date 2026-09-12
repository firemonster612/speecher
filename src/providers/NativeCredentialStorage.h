#pragma once

#include <QByteArray>
#include <QString>

namespace speecher {

QByteArray readNativeCredential(const QByteArray &service, const QByteArray &account, QString *error);
// Preflight the bounded macOS writer before rotating an OAuth token.
bool canWriteNativeCredential(const QByteArray &service, const QByteArray &account,
                              const QByteArray &bytes, QString *error);
// Checks that the entry still exists before updating it. This is not atomic
// with another process logging out or replacing the entry.
bool writeNativeCredential(const QByteArray &service, const QByteArray &account,
                           const QByteArray &bytes, QString *error);

} // namespace speecher
