#pragma once

#include "core/ProviderChain.h"
#include "core/settings/SettingsCodecs.h"

#include <QObject>
#include <QSettings>

#include <functional>

namespace speecher {

class SecretStore;

bool migrateSettingsIdentity(QSettings &newSettings,
                             QSettings &oldSettings,
                             QString *error = nullptr);
// Moves a saved refinement model that still names a replaced default to the
// current default. Runs once, so choosing the old model again later sticks.
void migrateRefinementModels(QSettings &settings);
// Paste with once also offered inserting directly and copying only, which the
// paste rules choose now. A stored method of either kind becomes Automatic,
// and every rule that pasted becomes Direct insertion or Clipboard only, so
// dictation delivers the way it did.
void migrateOutputMethod(QSettings &settings);
// Brings the settings an older Speecher saved up to date, before anything
// reads them: the migrations above, in order, on the running instance's
// store. Startup runs it once.
void migrateSavedSettings();

class SettingsStore : public QObject, private SettingsCodecs {
    Q_OBJECT

public:
    using LaunchAtLoginReconciler = std::function<bool(bool, QString *)>;

    explicit SettingsStore(QObject *parent = nullptr);
    // Carries the saved endpoint and CLI Proxy API keys, reading each one
    // the store hasn't as SecretStore::secret does: off the main thread the
    // read waits for the keyring (1.5 s at most), so the snapshot has them
    // all; on it, the read only starts, and the key stays in
    // unreadSecretKeys until it answers.
    AppSettings snapshot() const;
    // Never waits for the keyring on the GUI thread. Startup preparation
    // resolves unread keys for each provider it tries on its worker.
    AppSettings dictationSnapshot() const;
    // Whether this provider, in this role, needs a key the snapshot has not
    // read from the keyring yet.
    static bool hasUnreadProviderSecrets(const AppSettings &settings, ProviderRole role, const QString &providerId);
    // Reads those keys into settings, and no others, so one provider's
    // unreadable key never stops another. Run on the startup worker. Returns
    // an error if one is unreadable.
    static QString resolveProviderSecrets(AppSettings &settings, ProviderRole role, const QString &providerId);
    // Saves the draft's stored fields, unless deleteAll ran. The rest are
    // ignored; edit refuses a change to one. The keys the draft changed go
    // through SecretStore::saveSecrets: one the keyring refuses is only
    // logged, and one asked for while a write of it waits on the keyring is
    // saved after it. Moving the speech endpoint or the CLI Proxy API server
    // to another server clears its key, unless the draft entered a new one.
    void applySnapshot(const AppSettings &draft);
    // Saves what change makes of a dictation snapshot, unless it returns
    // false; returns whether it saved. The snapshot leaves unread keyring
    // secrets unread and applySnapshot keeps them, so an edit never clears a
    // key it didn't read. A change to a field that is never stored saves
    // nothing and logs the field.
    bool edit(const std::function<bool(AppSettings &)> &change);
    // Clears every setting and deletes the settings file, and the settings
    // an older Speecher left under its old app id, as removing Speecher with
    // its settings does. Afterwards nothing the store holds is saved: not a
    // snapshot, a setter, a codec's write or a sync of raw() (SettingsFile),
    // so an edit made before Speecher quits can't bring the file back. A file
    // that won't go puts its path in path; when the older settings won't, it
    // keeps only the deletion markers instead, so the next start doesn't
    // import them again, and goes on saving, so a removal tried again finds
    // them; path names the settings file too when that won't save. Only the
    // running app's store calls it (SettingsFile).
    bool deleteAll(QString *path);
    // Clears every setting but SecretStore's deletion markers, which hide
    // keys the keyring kept, saves that, and deletes the older settings
    // deleteAll does: what removing Speecher with its settings does when
    // deleting the file would show such a key. Once all of that went through,
    // as after deleteAll, nothing the store holds is saved afterwards, and the
    // settings file keeps the markers. The files that won't save or go put
    // their paths in path.
    bool deleteAllButDeletionMarkers(QString *path);
    SecretStore *secrets() const;
    // Starts Speecher at login, or stops it, as the saved launchAtLogin says.
    // applySnapshot runs it after every save.
    void setLaunchAtLoginReconciler(LaunchAtLoginReconciler reconcile);
    void reconcileLaunchAtLogin();
    // Announces learnedCorrectionsChanged when the evidence learned a
    // correction or strengthened one. Records nothing while correction
    // learning is off.
    bool recordCorrectionEvidence(const CorrectionEvidence &evidence,
                                  const QString &applicationId);
    // SettingsCodecs::recordVocabularyUsage, announcing the uses it counted.
    bool recordVocabularyUsage(const QString &text);
    // SettingsCodecs::addVocabularyTerms, announcing the terms it saved.
    // Saves nothing, returning nullopt, once deleteAll ran.
    std::optional<QStringList> addVocabularyTerms(const QStringList &terms);

    // Keys outside AppSettings, each read and written by the code that owns
    // it: the Global Shortcut binders' bindings, the updaters' bookkeeping,
    // the recording consent notice. None announces a change. The rest of the
    // settings go through snapshot() and edit().
    using SettingsCodecs::globalShortcut;
    using SettingsCodecs::setGlobalShortcut;
    using SettingsCodecs::forgetGlobalShortcutCombination;
    using SettingsCodecs::forgetGlobalShortcutSingleKey;
    using SettingsCodecs::globalShortcutCleared;
    using SettingsCodecs::portalShortcutCleared;
    using SettingsCodecs::setPortalShortcutCleared;
    using SettingsCodecs::updatesLastCheckTime;
    using SettingsCodecs::setUpdatesLastCheckTime;
    using SettingsCodecs::updatesDismissedVersion;
    using SettingsCodecs::setUpdatesDismissedVersion;
    using SettingsCodecs::updatesReopenMainWindow;
    using SettingsCodecs::updatesReopenMainWindowTime;
    using SettingsCodecs::setUpdatesReopenMainWindow;
    using SettingsCodecs::updatesLastRunVersion;
    using SettingsCodecs::setUpdatesLastRunVersion;
    using SettingsCodecs::updatesLastRunBuildNumber;
    using SettingsCodecs::setUpdatesLastRunBuildNumber;
    using SettingsCodecs::updatesPendingWhatsNewVersion;
    using SettingsCodecs::setUpdatesPendingWhatsNewVersion;
    using SettingsCodecs::recordingConsentNoticeShown;
    using SettingsCodecs::setRecordingConsentNoticeShown;
    // output.ydotoolEnabled, which edit() can't change: only
    // VirtualKeyboardSetup knows ydotool is set up.
    using SettingsCodecs::setYdotoolEnabled;

    SettingsFile &raw();

signals:
    // After applySnapshot, with what dictationSnapshot read just before it.
    void snapshotApplied(const AppSettings &previous);
    // Before snapshotApplied, when the save changed these settings.
    void audioCaptureSettingsChanged(const AudioCaptureSettings &settings);
    void correctionLearningEnabledChanged(bool enabled);
    // After recordCorrectionEvidence saved a learned correction.
    void learnedCorrectionsChanged();
    // Before snapshotApplied, when the save changed the update settings.
    void updateSettingsChanged();
    // After addVocabularyTerms saved terms no settings surface has drawn yet.
    void vocabularyAdded();
    void vocabularyUsageRecorded();

private:
    // previous is what dictationSnapshot read before the draft was made.
    // Returns whether it saved: false once deleteAll ran.
    bool applySnapshot(const AppSettings &draft, const AppSettings &previous);

    AppSettings snapshotReading(bool waitForKeyring) const;
    // Clears every setting but the deletion markers, marks the identity
    // migration done and saves that. A file that won't save puts its path in
    // path.
    bool keepOnlyDeletionMarkers(QString *path);

    LaunchAtLoginReconciler m_reconcileLaunchAtLogin;
    SecretStore *m_secrets = nullptr;
};

} // namespace speecher
