#pragma once

#include "platform/GlobalShortcutBinder.h"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QVariantMap>

namespace speecher {

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

using PortalShortcuts = QList<PortalShortcut>;

class PortalGlobalShortcutBinder final : public GlobalShortcutBinder,
                                         protected QDBusContext {
    Q_OBJECT

public:
    explicit PortalGlobalShortcutBinder(QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    void registerShortcut() override;
    QKeySequence shortcut() const override;
    QString shortcutDisplay() const override;
    bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) override;

private slots:
    void handleRequestResponse(uint response, const QVariantMap &results);
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
        None,
        CreateForRestore,
        CreateForRegistration,
        List,
        Bind,
    };

    bool ensureHostIdentity();
    void createSession(bool registration);
    void listShortcuts();
    void bindShortcuts();
    void sendRequest(const QString &member,
                     QVariantList arguments,
                     int optionsIndex,
                     RequestKind kind,
                     int timeoutMs);
    void requestFailed(const QString &reason, bool unsupported = false);
    void applyShortcuts(const QVariantMap &results);
    void closeSession();
    void disconnectRequest();

    bool m_supported = false;
    bool m_identityReady = false;
    QString m_unsupportedReason;
    QString m_triggerDescription;
    QDBusObjectPath m_sessionPath;
    QDBusObjectPath m_requestPath;
    RequestKind m_requestKind = RequestKind::None;
    quint64 m_requestGeneration = 0;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::PortalShortcut)
Q_DECLARE_METATYPE(speecher::PortalShortcuts)
