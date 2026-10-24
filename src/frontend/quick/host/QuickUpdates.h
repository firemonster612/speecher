#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace speecher {

class ApplicationController;
class UpdateController;

// The UI's Updates singleton: where the updater is in a check, download or
// restart, what it offers, and What's New, the news of the version an update
// installed. The UI words all of it.
class QuickUpdates final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Updates)
    QML_SINGLETON
    // idle, checking, checkFailed, upToDate, available, downloading, ready,
    // restartPending (waiting for a dictation or recording to end),
    // restarting or error (a download or install failed).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // What the offered update is called: a release's number, or a nightly's
    // build and commit.
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY changed)
    // A download's progress, 0 to 1.
    Q_PROPERTY(qreal progress READ progress NOTIFY changed)
    // Why the last check, download, install or restart failed, in core's
    // words; empty when nothing did.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Speecher downloads and installs an update itself; otherwise an update
    // opens the release page.
    Q_PROPERTY(bool automaticDownloads READ automaticDownloads CONSTANT)
    // Only installing by hand can work, so retrying opens the release page.
    Q_PROPERTY(bool manualInstall READ manualInstall NOTIFY changed)
    // The offer is the Stable Release, replacing the nightly running now.
    Q_PROPERTY(bool stableReplacement READ stableReplacement NOTIFY changed)
    // What a restart now would wait for: dictation, recording, or empty.
    Q_PROPERTY(QString restartBlocker READ restartBlocker NOTIFY changed)
    // When a check last succeeded; invalid before the first.
    Q_PROPERTY(QDateTime lastChecked READ lastChecked NOTIFY changed)
    // The update needs the person, so the popup and the sidebar offer it: an
    // offer not dismissed, a download, a restart not put off with later(),
    // or a failure worth telling.
    Q_PROPERTY(bool attention READ attention NOTIFY changed)
    // An update was installed and its news has not been seen.
    Q_PROPERTY(bool whatsNewPending READ whatsNewPending NOTIFY whatsNewPendingChanged)
    // The news, fixed for the run, so it stays on the page once seen:
    // { commits: [title] (a nightly's commits since the one it replaced),
    //   releases: [{ version, sections: [{ title, items: [text] }] }] }.
    Q_PROPERTY(QVariantMap whatsNew READ whatsNew CONSTANT)

public:
    // updates is the controller's updater; tests pass their own.
    QuickUpdates(ApplicationController *controller, UpdateController *updates, QObject *parent = nullptr);
    ~QuickUpdates() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickUpdates *create(QQmlEngine *, QJSEngine *);

    QString state() const;
    QString availableVersion() const;
    qreal progress() const;
    QString error() const;
    bool automaticDownloads() const;
    bool manualInstall() const;
    bool stableReplacement() const;
    QString restartBlocker() const;
    QDateTime lastChecked() const;
    bool attention() const;
    bool whatsNewPending() const;
    QVariantMap whatsNew() const;

    // Checks the chosen Update Channel.
    Q_INVOKABLE void check();
    // The next step for the state: download an offer (or open its release
    // page), restart into a downloaded one, or retry after a failure.
    Q_INVOKABLE void updateNow();
    // Downloads, installs and restarts in one go, for the popup's offer.
    Q_INVOKABLE void installAndRestart();
    // Puts off the waiting restart until another version is offered.
    Q_INVOKABLE void later();
    // Dismisses the offer of this version, or acknowledges a failure.
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void openAllReleases();
    // Whether a setting that came in sinceVersion is new since the version
    // run before this update.
    Q_INVOKABLE bool newSince(const QString &sinceVersion) const;
    // Marks the news seen.
    Q_INVOKABLE void acknowledgeWhatsNew();
    // Opens the news in the main window.
    Q_INVOKABLE void showWhatsNew();

signals:
    void changed();
    void whatsNewPendingChanged();
    // The main window should open on this page.
    void pageRequested(const QString &pageId);

private:
    ApplicationController *m_controller;
    UpdateController *m_updates;
    // The version run before the update, empty without one.
    QString m_lastSeenVersion;
    QVariantMap m_whatsNew;
    // The version later() put off.
    QString m_deferredVersion;
};

} // namespace speecher
