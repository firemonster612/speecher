#include "core/settings/SettingsFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>

#include <memory>

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
        save();
        QSettings::sync();
    }
}

bool SettingsFile::save()
{
    if (m_deleted) {
        return false;
    }
    // A QSettings opened now starts without an error. It shares this one's
    // unsaved changes (Qt keeps one copy of an open file), so the save its
    // sync makes is the one that writes them, and its status judges that
    // save alone. It opens this object's own file by name: deleteFile on
    // another SettingsFile points the organization and application at a
    // throwaway folder. macOS keeps native settings in a preferences domain,
    // which deleteFile never moves and a file name would open as a bare
    // plist, around the preferences daemon.
#ifdef Q_OS_MACOS
    const bool domain = format() == NativeFormat;
#else
    const bool domain = false;
#endif
    const auto saving = domain ? std::make_unique<QSettings>(format(), scope(), organizationName(), applicationName())
                               : std::make_unique<QSettings>(fileName(), format());
    saving->sync();
    if (saving->status() != QSettings::NoError) {
        return false;
    }
    ++m_saves;
    return true;
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
    // Otherwise save makes that save first, so saves() counts it.
    if (event->type() == QEvent::UpdateRequest) {
        if (m_deleted) {
            return true;
        }
        save();
    }
    return QSettings::event(event);
}

} // namespace speecher
