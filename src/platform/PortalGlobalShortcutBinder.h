#pragma once

#include "platform/GlobalShortcutBinder.h"

#include <QDBusObjectPath>
#include <QPointer>
#include <QVariantMap>

namespace speecher {

class PortalRequest;
class SettingsStore;

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

using PortalShortcuts = QList<PortalShortcut>;

class PortalGlobalShortcutBinder final : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit PortalGlobalShortcutBinder(
        SettingsStore *settings,
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);

    bool supported() const override;
    bool supportKnown() const override;
    bool usesDesktopShortcutChooser() const override;
    QString unsupportedReason() const override;
    void bind() override;
    void registerShortcut() override;
    ShortcutBinding shortcut() const override;
    QString shortcutDisplay() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    bool removeRegistration(QString *error = nullptr) override;

private slots:
    void handleActivated(const QDBusObjectPath &sessionHandle,
                         const QString &shortcutId,
                         qulonglong timestamp,
                         const QVariantMap &options);
    void handleDeactivated(const QDBusObjectPath &sessionHandle,
                           const QString &shortcutId,
                           qulonglong timestamp,
                           const QVariantMap &options);

private:
    enum class RequestKind {
        CreateForRestore,
        CreateForRegistration,
        List,
        Bind,
    };

    bool ensureHostIdentity(bool registration);
    void createSession(bool registration);
    void listShortcuts();
    void bindShortcuts();
    void sendRequest(const QString &method,
                     const QVariantList &arguments,
                     RequestKind kind,
                     int timeoutMs);
    void processRequestResponse(RequestKind kind, uint response, const QVariantMap &results);
    void requestFailed(RequestKind kind, const QString &reason);
    bool shortcutTrigger(const QVariantMap &results, QString *trigger) const;
    void activatePendingSession(const QString &trigger);
    void closePendingSession();
    void cancelRequest();

    SettingsStore *m_settings;
    bool m_supported = false;
    bool m_supportKnown = true;
    bool m_bindWhenSupported = false;
    bool m_registerWhenSupported = false;
    bool m_identityReady = false;
    bool m_identityPending = false;
    bool m_registrationAfterIdentity = false;
    // removeRegistration() ran while a Register was in flight; its watcher
    // must not create the session it was originally asked for.
    bool m_identityContinuationCancelled = false;
    QString m_unsupportedReason;
    QString m_triggerDescription;
    QDBusObjectPath m_sessionPath;
    QDBusObjectPath m_pendingSessionPath;
    // The session request in flight, if any.
    QPointer<PortalRequest> m_request;
    // The portal's timestamp of the press being held; zero when none is.
    qulonglong m_activatedAtMs = 0;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::PortalShortcut)
Q_DECLARE_METATYPE(speecher::PortalShortcuts)
