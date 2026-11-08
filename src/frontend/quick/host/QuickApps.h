#pragma once

#include "frontend/quick/host/HostSingleton.h"
#include "frontend/quick/host/QmlValues.h"
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
// paste keys by name ("standard", "terminal"), profiles by id; an empty
// value, or null steps, is unset ("Same as …"). Steps are { insert, press,
// keep }.
class QuickApps final : public QObject, public HostSingleton<QuickApps> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Apps)
    QML_SINGLETON
    // [{ id, name, icon, ids (what it is recognized by), matches ([{ kind:
    // "id" | "name" | "title", value }]), byText (matched by name or window
    // title, not an id), titleOnly, type, profile, steps, keys, history,
    // effective: { type, profile, steps, keys, history }, inherited: { type,
    // profile, profileFrom, steps, stepsFrom, keys, keysFrom, history,
    // historyFrom } }]: effective is what the app gets now, its steps the
    // Pasting page's where nothing sets them; inherited is what each value
    // would be without the app's own, with where it would come from
    // (builtIn, terminal, type or fallback), for "Same as …", null steps and
    // empty keys there being the Pasting page's and the usual ones. history
    // is "stats", "nothing" or empty: what History keeps of the app's
    // dictations, at most What Speecher Keeps.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    // One per App Type, Other apps ("general") last: [{ id, profile, steps,
    // keys, history, effectiveProfile, effective: { steps, keys } }]. Other
    // apps' profile is always set.
    Q_PROPERTY(QVariantList types READ types NOTIFY changed)
    // The installed apps, once refresh() has scanned them, launchers that
    // share a program as one under the first one's name: [{ name, icon, ids,
    // also (the other launchers' names), entry (the App that already matches
    // it, or empty), type, profile }], type and profile as core resolves
    // them.
    Q_PROPERTY(QVariantList installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
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
    // What testNext found, or empty: { name, appId, windowTitle, matched
    // (the ids of the Apps that match it, in the order core consults them),
    // type, profile, steps, keys, history, from: { profile, steps, keys,
    // history } }, each from { layer, app }: where the value came from, as
    // entries' inherited says, and the App that set it where layer is app.
    Q_PROPERTY(QVariantMap tested READ tested NOTIFY testedChanged)

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
    QVariantMap tested() const { return m_tested; }

    // Scans the installed apps off the UI thread, once a session; later
    // calls do nothing.
    Q_INVOKABLE void refresh();
    // Adds an App: { name, icon, ids } matches it by those ids, { name,
    // matches: [{ kind: "id" | "name" | "title", value }] } by what those say.
    // Returns { id, refusal }: the new App's id, or why core refused it.
    Q_INVOKABLE QVariantMap add(const QVariantMap &app);
    // Changes an App's type, profile, steps, keys, history, name or
    // matches (as add takes them, ids included); an empty value unsets it.
    // Returns why core refused it, empty when saved, or "invalid" for a
    // history other than "stats" or "nothing", which could only raise What
    // Speecher Keeps.
    Q_INVOKABLE QString update(const QString &id, const QVariantMap &patch);
    // Removes an App. Returns the token undo takes, or -1 when none went.
    Q_INVOKABLE int remove(const QString &id);
    Q_INVOKABLE void undo(int token);
    // Changes an App Type's profile, steps, keys or history, as update does.
    Q_INVOKABLE QString updateType(const QString &type, const QVariantMap &patch);
    // Switch to it: counts down `seconds`, waits for another app to come to
    // the front, then captures it as dictation does and answers captured
    // with the App it matches, added where none does.
    Q_INVOKABLE void captureNext(int seconds);
    // Test: captures the app in front as captureNext does, and answers
    // tested with what core makes of it, adding nothing.
    Q_INVOKABLE void testNext(int seconds);
    Q_INVOKABLE void cancelCapture();

signals:
    void changed();
    void installedChanged();
    void scanningChanged();
    void captureChanged();
    void testedChanged();
    // The App Switch to it found or added.
    void captured(const QString &id);

private:
    void rebuild();
    void rebuildInstalled();
    void setCapture(const QString &state);
    void captureFront();
    void startCapture(int seconds);
    void showTested(const Target &target);

    ApplicationController *m_controller;
    QList<InstalledApp> m_installed;
    QVariantList m_installedRecords;
    bool m_scanning = false;
    bool m_scanned = false;
    QVariantList m_entries;
    QVariantList m_types;
    QVariantList m_recent;
    UndoSlot m_undo;
    QString m_capture;
    // Whether the capture under way is a Test, which adds nothing.
    bool m_testing = false;
    QVariantMap m_tested;
    int m_countdown = 0;
    QTimer m_tick;
};

} // namespace speecher
