#include "core/settings/SettingsFile.h"

#include <QEvent>
#include <QFile>

namespace speecher {

SettingsFile::SettingsFile(const QString &organization, const QString &application)
    : QSettings(QSettings::defaultFormat(), QSettings::UserScope, organization, application)
{
}

SettingsFile::~SettingsFile()
{
    // ~QSettings saves what changed since; clearing and deleting first leaves
    // it nothing to save.
    if (m_deleted) {
        deleteFile();
    }
}

void SettingsFile::sync()
{
    if (!m_deleted) {
        QSettings::sync();
    }
}

bool SettingsFile::deleteFile()
{
    m_deleted = true;
    clear();
    QSettings::sync();
    const QString file = fileName();
    return !QFile::exists(file) || QFile::remove(file);
}

bool SettingsFile::event(QEvent *event)
{
    // QSettings saves on this event, posted after the first unsaved change.
    // Swallowed, it leaves the change unsaved, and QSettings posts no other.
    if (m_deleted && event->type() == QEvent::UpdateRequest) {
        return true;
    }
    return QSettings::event(event);
}

} // namespace speecher
