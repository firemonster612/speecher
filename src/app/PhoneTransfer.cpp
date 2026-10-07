#include "app/PhoneTransfer.h"

#include <monocypher.h>
#include <qrcodegen.hpp>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPainter>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

namespace speecher {

namespace {

constexpr int kFormat = 1;
constexpr qsizetype kTokenSize = 16;
constexpr qsizetype kKeySize = 32;
constexpr qsizetype kNonceSize = 12;
constexpr qsizetype kTagSize = 16;
constexpr int kMaxAddresses = 4;
constexpr int kTokenTimeoutMs = 10'000;
constexpr int kQuietZoneModules = 4;

QByteArray randomBytes(qsizetype size)
{
    Q_ASSERT(size % qsizetype(sizeof(quint32)) == 0);
    QByteArray bytes(size, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()),
                                          size / qsizetype(sizeof(quint32)));
    return bytes;
}

// Compares every byte whatever the first difference, so the time a wrong
// token takes says nothing about how much of it was right.
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

QString base64Url(const QByteArray &bytes)
{
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool isPrivateIPv4(const QHostAddress &address)
{
    static const QList<QPair<QHostAddress, int>> privateRanges{
        QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
    };
    return address.protocol() == QAbstractSocket::IPv4Protocol
        && std::any_of(privateRanges.begin(), privateRanges.end(),
                       [&](const auto &range) { return address.isInSubnet(range); });
}

} // namespace

QByteArray phoneTransferBundle(const AppSettings &settings, const QString &computerName)
{
    QJsonArray vocabulary;
    for (const VocabularyEntry &entry : settings.vocabulary) {
        vocabulary.append(QJsonObject{
            {QStringLiteral("term"), entry.term},
            {QStringLiteral("context"), entry.context},
            {QStringLiteral("profiles"), QJsonArray::fromStringList(entry.profiles)},
            {QStringLiteral("keyTerm"), entry.keyTerm},
            {QStringLiteral("priority"), entry.starred},
            {QStringLiteral("source"), entry.source},
            {QStringLiteral("frequency"), entry.frequency},
            {QStringLiteral("lastUsedMs"), entry.lastUsedMs},
        });
    }
    QJsonArray replacements;
    for (const BindingRule &rule : settings.bindings) {
        replacements.append(QJsonObject{
            {QStringLiteral("phrase"), rule.phrase},
            {QStringLiteral("text"), rule.replacement},
        });
    }
    const RefinementSettings &refinement = settings.refinement;
    QJsonArray profiles;
    for (const WritingProfileSettings &profile : refinement.writingProfiles) {
        profiles.append(QJsonObject{
            {QStringLiteral("id"), profile.profile},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("cleanupLevel"), profile.cleanupStrength},
            {QStringLiteral("tone"), profile.tone},
            {QStringLiteral("instructions"), profile.instructions},
            {QStringLiteral("outputLanguage"), profile.outputLanguage},
        });
    }
    QJsonArray tones;
    for (const CustomTone &tone : refinement.customTones) {
        tones.append(QJsonObject{
            {QStringLiteral("id"), tone.id},
            {QStringLiteral("name"), tone.name},
            {QStringLiteral("instruction"), tone.instruction},
        });
    }
    QJsonArray levels;
    for (const CustomCleanupLevel &level : refinement.customCleanupLevels) {
        levels.append(QJsonObject{
            {QStringLiteral("id"), level.id},
            {QStringLiteral("name"), level.name},
            {QStringLiteral("base"), level.base},
            {QStringLiteral("instructions"), level.instructions},
        });
    }
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("format"), kFormat},
                             {QStringLiteral("computer"), computerName},
                             {QStringLiteral("vocabulary"), vocabulary},
                             {QStringLiteral("replacements"), replacements},
                             {QStringLiteral("writingProfiles"), profiles},
                             {QStringLiteral("customTones"), tones},
                             {QStringLiteral("customCleanupLevels"), levels},
                             {QStringLiteral("additionalInstructions"),
                              refinement.additionalInstructions},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray sealPhoneTransfer(const QByteArray &plain, const QByteArray &key,
                             const QByteArray &nonce)
{
    Q_ASSERT(key.size() == kKeySize && nonce.size() == kNonceSize);
    QByteArray sealed = nonce;
    sealed.resize(kNonceSize + plain.size() + kTagSize);
    auto *cipherText = reinterpret_cast<uint8_t *>(sealed.data()) + kNonceSize;
    crypto_aead_ctx context;
    crypto_aead_init_ietf(&context, reinterpret_cast<const uint8_t *>(key.constData()),
                          reinterpret_cast<const uint8_t *>(nonce.constData()));
    crypto_aead_write(&context, cipherText, cipherText + plain.size(), nullptr, 0,
                      reinterpret_cast<const uint8_t *>(plain.constData()),
                      static_cast<size_t>(plain.size()));
    crypto_wipe(&context, sizeof(context));
    return sealed;
}

QString phoneTransferLink(const QStringList &addresses, quint16 port, const QByteArray &token,
                          const QByteArray &key, const QString &computerName)
{
    return QStringLiteral("speecher://import?v=%1&a=%2&p=%3&t=%4&k=%5&n=%6")
        .arg(kFormat)
        .arg(addresses.join(QLatin1Char(',')))
        .arg(port)
        .arg(base64Url(token), base64Url(key),
             QString::fromLatin1(QUrl::toPercentEncoding(computerName)));
}

QStringList phoneTransferAddresses()
{
    // Ranked so the phone, which tries them in turn, reaches the likeliest
    // first: a phone is on Wi-Fi, so Wi-Fi, then wired, then anything else.
    QList<QPair<int, QString>> ranked;
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)
            || flags.testFlag(QNetworkInterface::IsPointToPoint)
            || interface.type() == QNetworkInterface::Virtual) {
            continue;
        }
        const int rank = interface.type() == QNetworkInterface::Wifi       ? 0
                         : interface.type() == QNetworkInterface::Ethernet ? 1
                                                                            : 2;
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            if (isPrivateIPv4(entry.ip())) {
                ranked.append({rank, entry.ip().toString()});
            }
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList addresses;
    for (const auto &[rank, address] : std::as_const(ranked).first(
             std::min<qsizetype>(ranked.size(), kMaxAddresses))) {
        addresses.append(address);
    }
    return addresses;
}

QImage qrCodeImage(const QString &text, int maximumSide)
{
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
        text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = code.getSize() + 2 * kQuietZoneModules;
    const int moduleSize = std::max(1, maximumSide / modules);
    const int side = modules * moduleSize;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < code.getSize(); ++y) {
        for (int x = 0; x < code.getSize(); ++x) {
            if (code.getModule(x, y)) {
                painter.fillRect((x + kQuietZoneModules) * moduleSize,
                                 (y + kQuietZoneModules) * moduleSize, moduleSize, moduleSize,
                                 Qt::black);
            }
        }
    }
    return image;
}

PhoneTransfer::PhoneTransfer(const AppSettings &settings, QObject *parent)
    : PhoneTransfer(settings, phoneTransferAddresses(), parent)
{
}

PhoneTransfer::PhoneTransfer(const AppSettings &settings, const QStringList &addresses,
                             QObject *parent)
    : QObject(parent)
    , m_addresses(addresses)
    , m_computerName(QSysInfo::machineHostName())
    , m_token(randomBytes(kTokenSize))
    , m_key(randomBytes(kKeySize))
{
    if (m_addresses.isEmpty()) {
        m_state = PhoneTransferState::NoNetwork;
        return;
    }
    if (!m_server.listen(QHostAddress::AnyIPv4)) {
        qWarning().noquote() << "phone transfer listen failed message=" + m_server.errorString();
        m_state = PhoneTransferState::Failed;
        return;
    }
    m_sealed = sealPhoneTransfer(phoneTransferBundle(settings, m_computerName), m_key,
                                 randomBytes(kNonceSize));
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            serve(socket);
        }
    });
}

QString PhoneTransfer::link() const
{
    if (m_state != PhoneTransferState::Waiting) {
        return {};
    }
    return phoneTransferLink(m_addresses, m_server.serverPort(), m_token, m_key, m_computerName);
}

void PhoneTransfer::serve(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    // Every connection is judged on its own, so one that never sends a token
    // holds only itself, and only this long.
    QTimer::singleShot(kTokenTimeoutMs, socket, [socket] {
        if (!socket->property("answered").toBool()) {
            socket->abort();
        }
    });
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
        if (socket->property("answered").toBool() || socket->bytesAvailable() < kTokenSize) {
            return;
        }
        if (m_state != PhoneTransferState::Waiting || !m_server.isListening()
            || !sameBytes(socket->read(kTokenSize), m_token)) {
            socket->abort();
            return;
        }
        // Served once: nobody else may connect, even with the code.
        m_server.close();
        socket->setProperty("answered", true);
        QByteArray length(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(m_sealed.size()), length.data());
        connect(socket, &QTcpSocket::bytesWritten, this, [this, socket] {
            if (socket->bytesToWrite() == 0) {
                socket->disconnectFromHost();
                setState(PhoneTransferState::Sent);
            }
        });
        socket->write(length + m_sealed);
    });
}

void PhoneTransfer::setState(PhoneTransferState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

} // namespace speecher
