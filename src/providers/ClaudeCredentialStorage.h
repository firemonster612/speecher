#pragma once

#include <QByteArray>
#include <QString>

namespace speecher {

// Selected once for a load/refresh. A missing or changed item during refresh
// must not redirect rotated tokens into a different credential store.
class ClaudeCredentialStorage {
public:
    explicit ClaudeCredentialStorage(const QString &path);
    QByteArray read(QString *error) const;
    bool write(const QByteArray &bytes, QString *error) const;
    QString lockPath() const;

private:
    QString m_path;
    QByteArray m_service;
    QByteArray m_account;
};

} // namespace speecher
