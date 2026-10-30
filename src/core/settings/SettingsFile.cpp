#include "core/settings/SettingsFile.h"

#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

namespace speecher {

namespace {

#ifdef Q_OS_UNIX
constexpr QFile::Permissions groupAndOthers = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
    | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;

// Takes every permission from the group and other users.
void keepToOwner(const QString &path)
{
    QFile::setPermissions(path, QFile::permissions(path) & ~groupAndOthers);
}
#endif

// A folder removed only as it was made: through a symlink put in its place,
// removing it would empty the folder the link points at.
class ThrowawayFolder final : public QTemporaryDir {
public:
    using QTemporaryDir::QTemporaryDir;
    ~ThrowawayFolder()
    {
        const QFileInfo folder(path());
        setAutoRemove(folder.isDir() && !folder.isSymLink());
    }
};

// Where QSettings made after a deletion keep their files: a reader made then
// (a provider check's SettingsStore) would otherwise share the deleted
// file's unsaved changes and save them, bringing the file back. Private and
// under a random name, so no other user can have put a link or a file there
// first. It lasts as long as the process: QSettings made at any later point
// write there, and would make a removed folder again, open to others.
QString throwawaySettingsHome()
{
    static const ThrowawayFolder home(QDir::temp().filePath(QStringLiteral("speecher-removed-XXXXXX")));
    if (!home.isValid()) {
        qWarning().noquote() << "Could not make a folder for settings opened after the deletion:"
                             << home.errorString();
    }
    return home.path();
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
#ifdef Q_OS_UNIX
    // The folder first: a file QSettings makes takes the umask's
    // permissions, open to others until they are taken below.
    const QString folder = QFileInfo(fileName()).absolutePath();
    if (!domain && QDir().mkpath(folder)) {
        keepToOwner(folder);
    }
#endif
    saving->sync();
    if (saving->status() != QSettings::NoError) {
        return false;
    }
#ifdef Q_OS_UNIX
    if (!domain) {
        keepToOwner(fileName());
    }
#endif
    ++m_saves;
    return true;
}

bool SettingsFile::deleteFile()
{
    if (!m_deleted) {
        // Both scopes: a reader also opens the system scope's file as a
        // fallback, and saves whatever changed in any file it opened.
        const QString home = throwawaySettingsHome();
        QSettings::setPath(format(), QSettings::UserScope, home);
        QSettings::setPath(format(), QSettings::SystemScope, home + QStringLiteral("/system"));
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
