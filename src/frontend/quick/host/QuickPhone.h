#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

#include <memory>

namespace speecher {

class ApplicationController;
class PhoneTransfer;

// The UI's Phone singleton: the code that sends this computer's settings to
// Speecher for Android. The code exists from start to stop: Speecher listens
// for the phone only while it shows. Starting again makes a new code.
class QuickPhone final : public QObject, public HostSingleton<QuickPhone> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Phone)
    QML_SINGLETON
    // Empty while no code is shown; otherwise waiting, sent, interrupted,
    // noNetwork or failed (PhoneTransferState).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // What the code carries while it waits; empty otherwise.
    Q_PROPERTY(QString link READ link NOTIFY changed)
    // What the last code shown sends: {terms, replacements, profiles} counts.
    Q_PROPERTY(QVariantMap bundle READ bundle NOTIFY changed)

public:
    explicit QuickPhone(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickPhone() override;

    QString state() const;
    QString link() const;
    QVariantMap bundle() const { return m_bundle; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    ApplicationController *m_controller;
    std::unique_ptr<PhoneTransfer> m_transfer;
    QVariantMap m_bundle;
};

} // namespace speecher
