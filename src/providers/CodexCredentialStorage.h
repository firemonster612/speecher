#pragma once

#include <QByteArray>
#include <QString>

namespace speecher {

// File first, native fallback only if auth.json is absent. Keep this selection
// through an entire resolution/refresh, even if another login changes stores.
class CodexCredentialStorage {
public:
    CodexCredentialStorage();
    QByteArray read(QString *error) const;
    bool canWrite(const QByteArray &bytes, QString *error) const;
    bool write(const QByteArray &bytes, QString *error) const;
    QString lockPath() const;

private:
    QString m_path;
    QByteArray m_account;
};

} // namespace speecher
