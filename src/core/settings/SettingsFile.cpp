#include "core/settings/SettingsFile.h"

#include "core/OwnerOnly.h"

#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

namespace speecher {

namespace {

// Whether the settings live in a macOS preferences domain, which the
// preferences daemon writes, rather than in a file of their own.
bool inPreferencesDomain(const QSettings &settings)
{
#ifdef Q_OS_MACOS
    return settings.format() == QSettings::NativeFormat;
#else
    Q_UNUSED(settings);
    return false;
#endif
}

#ifdef Q_OS_UNIX
// Takes every permission from the group and other users, and says whether
// path is then this user's alone.
bool keepToOwner(const QString &path)
{
    QFile::setPermissions(path, QFile::permissions(path) & ~groupAndOthers);
    return isOwnerOnly(path);
}
#endif

// Where QSettings made after a deletion keep their files: a reader made then
// (a provider check's SettingsStore) would otherwise share the deleted
// file's unsaved changes and save them, bringing the file back. Private and
// under a random name, so no other user can have put a link or a file there
// first. It lasts as long as the process: QSettings made at any later point
// write there, and would make a removed folder again, open to others.
QString throwawaySettingsHome()
{
    static const QTemporaryDir home(QDir::temp().filePath(QStringLiteral("speecher-removed-XXXXXX")));
    if (!home.isValid()) {
        qWarning().noquote() << "Could not make a folder for settings opened after the deletion:"
                             << home.errorString();
    }
    return home.path();
}

// Where QSettings made after SettingsFile::close open the settings'
// file, in the throwaway folder; nothing where QSettings::setPath doesn't
// reach, as for the Windows registry and a macOS preferences domain.
QString throwawayFile(const QSettings &settings)
{
#ifdef Q_OS_WIN
    if (settings.format() == QSettings::NativeFormat) {
        return {};
    }
#endif
    const QString home = throwawaySettingsHome();
    if (home.isEmpty() || inPreferencesDomain(settings)) {
        return {};
    }
    const QFileInfo file(settings.fileName());
    return home + QLatin1Char('/') + file.dir().dirName() + QLatin1Char('/') + file.fileName();
}

// Makes settings hold kept alone, and writes that.
void writeOnly(QSettings &settings, const QVariantMap &kept)
{
    settings.clear();
    for (auto it = kept.cbegin(); it != kept.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.sync();
}

} // namespace

bool makeSettingsPrivate(const QSettings &settings, QString *error)
{
#ifdef Q_OS_UNIX
    if (inPreferencesDomain(settings)) {
        return true;
    }
    const QString file = settings.fileName();
    const QString folder = QFileInfo(file).absolutePath();
    QString refusal;
    if (!QDir().mkpath(folder) || !keepToOwner(folder)) {
        refusal = QStringLiteral("the folder %1 can't be kept from other users").arg(folder);
    } else {
        // Made now if missing: QSettings keeps a file's permissions when it
        // rewrites it, but gives a file it makes the umask's.
        QFile made(file);
        if ((!made.exists() && !made.open(QIODevice::WriteOnly | QIODevice::Append)) || !keepToOwner(file)) {
            refusal = QStringLiteral("the settings file %1 can't be kept from other users").arg(file);
        }
    }
    if (!refusal.isEmpty() && error) {
        *error = refusal;
    }
    return refusal.isEmpty();
#else
    Q_UNUSED(settings);
    Q_UNUSED(error);
    return true;
#endif
}

SettingsFile::SettingsFile(const QString &organization, const QString &application)
    : QSettings(QSettings::defaultFormat(), QSettings::UserScope, organization, application)
{
}

SettingsFile::~SettingsFile()
{
    // ~QSettings saves what changed since; closing again first leaves it
    // nothing else to save, and deletes the file again for whatever another
    // QSettings wrote meanwhile.
    if (m_closed) {
        close(m_kept);
    }
}

void SettingsFile::sync()
{
    if (!m_closed) {
        save();
        QSettings::sync();
    }
}

bool SettingsFile::save()
{
    if (m_closed) {
        return false;
    }
    // A QSettings opened now starts without an error. It shares this one's
    // unsaved changes (Qt keeps one copy of an open file), so the save its
    // sync makes is the one that writes them, and its status judges that
    // save alone. It opens this object's own file by name: close on
    // another SettingsFile points the organization and application at a
    // throwaway folder. macOS keeps native settings in a preferences domain,
    // which close never moves and a file name would open as a bare
    // plist, around the preferences daemon.
    const auto saving = inPreferencesDomain(*this)
        ? std::make_unique<QSettings>(format(), scope(), organizationName(), applicationName())
        : std::make_unique<QSettings>(fileName(), format());
    // As private as it can be made. Settings that can't be still save: a key
    // goes in only once makeSettingsPrivate succeeds (SecretStore).
    makeSettingsPrivate(*this, nullptr);
    saving->sync();
    if (saving->status() != QSettings::NoError) {
        return false;
    }
    ++m_saves;
    return true;
}

bool SettingsFile::close(const QVariantMap &kept)
{
    if (!m_closed) {
        // Seeded first, so no reader finds it without what is kept: a
        // deletion marker hides a key from readers made in this process too.
        if (const QString file = throwawayFile(*this); !file.isEmpty()) {
            QSettings reader(file, format());
            writeOnly(reader, kept);
        }
        // Both scopes: a reader also opens the system scope's file as a
        // fallback, and saves whatever changed in any file it opened.
        const QString home = throwawaySettingsHome();
        QSettings::setPath(format(), QSettings::UserScope, home);
        QSettings::setPath(format(), QSettings::SystemScope, home + QStringLiteral("/system"));
    }
    m_closed = true;
    m_kept = kept;
    writeOnly(*this, kept);
    const QString file = fileName();
    return !kept.isEmpty() || !QFile::exists(file) || QFile::remove(file);
}

bool SettingsFile::event(QEvent *event)
{
    // QSettings saves on this event, posted after the first unsaved change.
    // Swallowed, it leaves the change unsaved, and QSettings posts no other.
    // Otherwise save makes that save first, so saves() counts it.
    if (event->type() == QEvent::UpdateRequest) {
        if (m_closed) {
            return true;
        }
        save();
    }
    return QSettings::event(event);
}

} // namespace speecher
