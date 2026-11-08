#pragma once

#include "core/SettingsEdit.h"
#include "frontend/quick/host/HostSingleton.h"
#include "platform/InstalledApps.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

namespace speecher {

class ApplicationController;

// The UI's Apps singleton: the person's Apps and App Types as core resolves
// them, the installed and recently dictated-into apps the picker offers, and
// the edits the Apps page makes. Data only; the page words it
// (schema/apps.js). Ids are core's: App Types by category name ("email"),
// Pasting choices by paste method name, profiles by id; an empty value is
// unset ("Same as …").
class QuickApps final : public QObject, public HostSingleton<QuickApps> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Apps)
    QML_SINGLETON
    // [{ id, name, icon, ids (what it is recognized by), byText (matched by
    // name or window title, not an id), titleOnly, type, profile, pasting,
    // effective: { type, profile, pasting }, inherited: { type, profile,
    // profileFrom, pasting, pastingFrom } }]: effective is what the
    // app gets now; inherited is what each value would be without the app's
    // own, with where it would come from (builtIn, terminal, type or
    // fallback), for "Same as …".
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    // One per App Type, Other apps ("general") last: [{ id, profile, pasting,
    // effectiveProfile }]. Other apps' profile is always set.
    Q_PROPERTY(QVariantList types READ types NOTIFY changed)
    // The installed apps, once refresh() has scanned them: [{ name, icon,
    // ids, entry (the App that already matches it, or empty), type, profile
    // }], type and profile as core resolves them.
    Q_PROPERTY(QVariantList installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY installedChanged)
    // The apps dictated into lately, most recent first, while History keeps
    // stats: [{ name, appId, icon, lastSeenMs, dictations, entry, type,
    // profile }].
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
    // How many apps the built-in catalog knows.
    Q_PROPERTY(int builtInCount READ builtInCount CONSTANT)
    // Switch to it: "" (idle), "waiting" (counting down, then waiting for
    // another app to come to the front), "failed" (it couldn't tell which
    // app that was).
    Q_PROPERTY(QString capture READ capture NOTIFY captureChanged)
    Q_PROPERTY(int captureCountdown READ captureCountdown NOTIFY captureChanged)

public:
    explicit QuickApps(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickApps() override;

    QVariantList entries() const { return m_entries; }
    QVariantList types() const { return m_types; }
    QVariantList installed() const { return m_installedRecords; }
    bool scanning() const { return m_scanning; }
    QVariantList recent() const { return m_recent; }
    int builtInCount() const;
    QString capture() const { return m_capture; }
    int captureCountdown() const { return m_countdown; }

    // Scans the installed apps off the UI thread, once a session; later
    // calls do nothing.
    Q_INVOKABLE void refresh();
    // Adds an App: { name, icon, ids } matches it by those ids, { name,
    // matches: [{ kind: "name" | "title", value }] } by name or window title.
    // Returns { id, refusal }: the new App's id, or why core refused it.
    Q_INVOKABLE QVariantMap add(const QVariantMap &app);
    // Changes an App's type, profile, pasting or name; an empty value unsets
    // it. Returns why core refused it, empty when saved.
    Q_INVOKABLE QString update(const QString &id, const QVariantMap &patch);
    // Removes an App. Returns the token undo takes, or -1 when none went.
    Q_INVOKABLE int remove(const QString &id);
    Q_INVOKABLE void undo(int token);
    // Changes an App Type's profile or pasting, as update does.
    Q_INVOKABLE QString updateType(const QString &type, const QVariantMap &patch);
    // Switch to it: counts down `seconds`, waits for another app to come to
    // the front, then captures it as dictation does and answers captured
    // with the App it matches, added where none does.
    Q_INVOKABLE void captureNext(int seconds);
    Q_INVOKABLE void cancelCapture();

signals:
    void changed();
    void installedChanged();
    void captureChanged();
    // The App Switch to it found or added.
    void captured(const QString &id);

private:
    void rebuild();
    void rebuildInstalled();
    void setCapture(const QString &state);
    void captureFront();
    QString saveUnlessRefused(const std::function<EditRefusal(AppSettings &)> &edit);

    ApplicationController *m_controller;
    QList<InstalledApp> m_installed;
    QVariantList m_installedRecords;
    bool m_scanning = false;
    bool m_scanned = false;
    QVariantList m_entries;
    QVariantList m_types;
    QVariantList m_recent;
    SettingsEdit m_undo;
    int m_lastUndo = -1;
    QString m_capture;
    int m_countdown = 0;
    QTimer m_tick;
};

} // namespace speecher
