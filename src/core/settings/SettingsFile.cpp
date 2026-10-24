#include "core/settings/SettingsFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>

namespace speecher {

namespace {

// Where QSettings made from now on keep their files: a reader made after the
// deletion (a provider check's SettingsStore) would otherwise share this
// file's unsaved changes and save them, bringing the file back.
QString deletedSettingsHome()
{
    return QDir::temp().filePath(QStringLiteral("speecher-removed-%1").arg(QCoreApplication::applicationPid()));
}

} // namespace

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
        QDir(deletedSettingsHome()).removeRecursively();
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
    if (!m_deleted) {
        // Both scopes: a reader also opens the system scope's file as a
        // fallback, and saves whatever changed in any file it opened.
        QSettings::setPath(format(), QSettings::UserScope, deletedSettingsHome());
        QSettings::setPath(format(), QSettings::SystemScope, deletedSettingsHome() + QStringLiteral("/system"));
    }
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
