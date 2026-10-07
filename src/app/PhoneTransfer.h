#pragma once

#include "core/AppSettings.h"

#include <QImage>
#include <QObject>
#include <QTcpServer>

// Copying settings to the Android app: the desktop shows a QR code, the phone
// connects and receives the settings encrypted with the key in the code. The
// wire format is docs/adr/0008-phone-transfer-over-the-lan.md.

namespace speecher {

// The ADR's bundle: the user's own dictation setup as UTF-8 JSON, never
// credentials.
QByteArray phoneTransferBundle(const AppSettings &settings, const QString &computerName);

// plain sealed with ChaCha20-Poly1305 (RFC 8439): nonce, ciphertext, tag.
QByteArray sealPhoneTransfer(const QByteArray &plain, const QByteArray &key,
                             const QByteArray &nonce);

// The speecher://import link the code carries.
QString phoneTransferLink(const QStringList &addresses, quint16 port, const QByteArray &token,
                          const QByteArray &key, const QString &computerName);

// The private IPv4 addresses a phone on the same network may reach, Wi-Fi
// first, at most four.
QStringList phoneTransferAddresses();

// text as a QR code, black on white with the standard four-module quiet zone,
// as large as fits in maximumSide pixels with every module a whole number of
// pixels, so no module blurs into its neighbour.
QImage qrCodeImage(const QString &text, int maximumSide);

enum class PhoneTransferState {
    Waiting,
    Sent,
    // The phone took the code but left before it had the settings.
    Interrupted,
    // No address a phone could reach, so no code to show.
    NoNetwork,
    // The system refused a listening port.
    Failed,
};

// One transfer: listens from construction until destruction, which is as long
// as the dialog showing its code is open, and serves the bundle once.
class PhoneTransfer : public QObject {
    Q_OBJECT

public:
    explicit PhoneTransfer(const AppSettings &settings, QObject *parent = nullptr);
    // For tests: the addresses to advertise instead of the machine's own.
    PhoneTransfer(const AppSettings &settings, const QStringList &addresses,
                  QObject *parent = nullptr);

    PhoneTransferState state() const { return m_state; }
    // What the QR code carries; empty unless Waiting.
    QString link() const;

signals:
    void stateChanged();

private:
    qsizetype openConnections() const;
    void serve(QTcpSocket *socket);
    void setState(PhoneTransferState state);

    QTcpServer m_server;
    PhoneTransferState m_state = PhoneTransferState::Waiting;
    QStringList m_addresses;
    QString m_computerName;
    QByteArray m_token;
    QByteArray m_key;
    QByteArray m_sealed;
};

} // namespace speecher
