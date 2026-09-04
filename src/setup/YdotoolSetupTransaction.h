#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

class YdotoolSetupTransaction {
public:
    void record(const QString &change)
    {
        m_changes.append(change);
    }

    void appendToError(QString *error) const
    {
        if (!error || m_changes.isEmpty()) {
            return;
        }
        *error += QStringLiteral("\nChanges left behind:\n- ")
            + m_changes.join(QStringLiteral("\n- "));
    }

private:
    QStringList m_changes;
};

} // namespace speecher
