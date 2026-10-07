#include "common/test_suites.h"
#include "app/PhoneTransfer.h"
#include "app/PhoneTransferPresentation.h"
#include "core/PasteRules.h"

#include <monocypher.h>

#include <QSignalSpy>
#include <QSysInfo>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtEndian>

using namespace speecher;

namespace {

QByteArray bytesFrom(int first, int count)
{
    QByteArray bytes;
    for (int i = 0; i < count; ++i) {
        bytes.append(char(first + i));
    }
    return bytes;
}

AppSettings sampleSettings()
{
    AppSettings settings;
    VocabularyEntry kubernetes;
    kubernetes.term = QStringLiteral("Kubernetes");
    kubernetes.starred = true;
    kubernetes.frequency = 41;
    kubernetes.lastUsedMs = 1790000000000;
    kubernetes.context = QStringLiteral("The container platform.");
    kubernetes.profiles = {QStringLiteral("work")};
    settings.vocabulary = {kubernetes};
    settings.bindings = {{QStringLiteral("my address"), QStringLiteral("Flat 3")}};
    WritingProfileSettings work;
    work.profile = QStringLiteral("work");
    work.cleanupStrength = QStringLiteral("custom_terse");
    work.tone = QStringLiteral("formal");
    work.instructions = QStringLiteral("Short.");
    settings.refinement.writingProfiles = {work};
    settings.refinement.customTones = {
        {QStringLiteral("custom_dry"), QStringLiteral("Dry"), QStringLiteral("Understated.")}};
    settings.refinement.customCleanupLevels = {{QStringLiteral("custom_terse"),
                                                QStringLiteral("Terse"),
                                                QStringLiteral("strong_polish"),
                                                QStringLiteral("Cut filler.")}};
    settings.refinement.additionalInstructions = QStringLiteral("Use British spelling.");
    // Credentials, which must never leave the computer.
    settings.refinement.cliproxyApiKey = QStringLiteral("proxy-secret");
    settings.speech.endpoint.apiKey = QStringLiteral("speech-secret");
    return settings;
}

struct LinkParts {
    quint16 port = 0;
    QByteArray token;
    QByteArray key;
};

LinkParts partsOf(const QString &link)
{
    const QUrlQuery query(QUrl(link).query());
    const auto decoded = [&](const QString &name) {
        return QByteArray::fromBase64(query.queryItemValue(name).toLatin1(),
                                      QByteArray::Base64UrlEncoding);
    };
    return {quint16(query.queryItemValue(QStringLiteral("p")).toUInt()),
            decoded(QStringLiteral("t")), decoded(QStringLiteral("k"))};
}

// Sends token and returns everything the transfer answers before closing.
// Event-driven, because the transfer answers on this thread's event loop. Windows
// takes about four seconds to report a refused connection, hence the wait.
QByteArray exchange(quint16 port, const QByteArray &token)
{
    QTcpSocket socket;
    QByteArray answer;
    QObject::connect(&socket, &QTcpSocket::connected, [&] { socket.write(token); });
    QObject::connect(&socket, &QTcpSocket::readyRead, [&] { answer += socket.readAll(); });
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool closed =
        QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::UnconnectedState; }, 10000);
    return closed ? answer : QByteArrayLiteral("<still open>");
}

} // namespace

class PhoneTransferTests : public QObject {
    Q_OBJECT

private slots:
    // The expected bytes come from Python's cryptography package, an
    // implementation independent of Monocypher; the Android tests use the same.
    void sealMatchesAnIndependentImplementation()
    {
        const QByteArray sealed = sealPhoneTransfer(
            QByteArrayLiteral("{\"format\":1,\"computer\":\"test\"}"), bytesFrom(0, 32),
            bytesFrom(0xa0, 12));
        QCOMPARE(sealed.toHex(),
                 QByteArrayLiteral("a0a1a2a3a4a5a6a7a8a9aaab77891e303f8ba3d98235c238de999296ed2b"
                                   "a7da31485a81c0bda8c5531f993f914144b39ebecba5459cac59a143"));
    }

    void bundleCarriesOnlyTheDictationSetup()
    {
        QCOMPARE(
            phoneTransferBundle(sampleSettings(), QStringLiteral("desk")),
            QByteArrayLiteral(
                "{\"additionalInstructions\":\"Use British spelling.\",\"computer\":\"desk\","
                "\"customCleanupLevels\":[{\"base\":\"strong_polish\",\"id\":\"custom_terse\","
                "\"instructions\":\"Cut filler.\",\"name\":\"Terse\"}],"
                "\"customTones\":[{\"id\":\"custom_dry\",\"instruction\":\"Understated.\","
                "\"name\":\"Dry\"}],\"format\":1,"
                "\"replacements\":[{\"phrase\":\"my address\",\"text\":\"Flat 3\"}],"
                "\"vocabulary\":[{\"context\":\"The container platform.\",\"frequency\":41,"
                "\"keyTerm\":true,\"lastUsedMs\":1790000000000,\"priority\":true,"
                "\"profiles\":[\"work\"],\"source\":\"manual\",\"term\":\"Kubernetes\"}],"
                "\"writingProfiles\":[{\"cleanupLevel\":\"custom_terse\",\"id\":\"work\","
                "\"instructions\":\"Short.\",\"name\":\"\",\"outputLanguage\":\"\","
                "\"tone\":\"formal\"}]}"));
    }

    void linkCarriesAddressesPortTokenKeyAndName()
    {
        QCOMPARE(phoneTransferLink({QStringLiteral("192.168.1.20"), QStringLiteral("10.0.0.5")},
                                   53817, bytesFrom(0, 16), bytesFrom(0, 32),
                                   QStringLiteral("enzo's pc+1")),
                 QStringLiteral("speecher://import?v=1&a=192.168.1.20,10.0.0.5&p=53817"
                                "&t=AAECAwQFBgcICQoLDA0ODw"
                                "&k=AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8"
                                "&n=enzo%27s%20pc%2B1"));
    }

    void servesTheBundleOnceAndOnlyToTheTokenHolder()
    {
        const AppSettings settings = sampleSettings();
        PhoneTransfer transfer(settings, {QStringLiteral("127.0.0.1")});
        QCOMPARE(transfer.state(), PhoneTransferState::Waiting);
        const LinkParts parts = partsOf(transfer.link());
        QCOMPARE(parts.token.size(), 16);
        QCOMPARE(parts.key.size(), 32);

        QByteArray wrongToken = parts.token;
        wrongToken[0] = char(wrongToken[0] ^ 1);
        QCOMPARE(exchange(parts.port, wrongToken), QByteArray());
        QCOMPARE(transfer.state(), PhoneTransferState::Waiting);

        QSignalSpy sent(&transfer, &PhoneTransfer::stateChanged);
        const QByteArray answer = exchange(parts.port, parts.token);
        QVERIFY(sent.count() == 1 || sent.wait(2000));
        QCOMPARE(transfer.state(), PhoneTransferState::Sent);
        QCOMPARE(transfer.link(), QString());

        QVERIFY(answer.size() > 4 + 12 + 16);
        QCOMPARE(qFromBigEndian<quint32>(answer.constData()), quint32(answer.size() - 4));
        const QByteArray sealed = answer.mid(4);
        const QByteArray cipherText = sealed.mid(12, sealed.size() - 12 - 16);
        QByteArray plain(cipherText.size(), Qt::Uninitialized);
        crypto_aead_ctx context;
        crypto_aead_init_ietf(&context, reinterpret_cast<const uint8_t *>(parts.key.constData()),
                              reinterpret_cast<const uint8_t *>(sealed.constData()));
        QCOMPARE(crypto_aead_read(&context, reinterpret_cast<uint8_t *>(plain.data()),
                                  reinterpret_cast<const uint8_t *>(sealed.constData())
                                      + sealed.size() - 16,
                                  nullptr, 0,
                                  reinterpret_cast<const uint8_t *>(cipherText.constData()),
                                  size_t(cipherText.size())),
                 0);
        QCOMPARE(plain, phoneTransferBundle(settings, QSysInfo::machineHostName()));

        QCOMPARE(exchange(parts.port, parts.token), QByteArray());
    }

    void withoutANetworkThereIsNoCode()
    {
        PhoneTransfer transfer(AppSettings{}, QStringList{});
        QCOMPARE(transfer.state(), PhoneTransferState::NoNetwork);
        QCOMPARE(transfer.link(), QString());
        QVERIFY(phoneTransferText(AppSettings(), transfer.state()).steps.isEmpty());
    }

    void dialogListsWhatMovesAndWhatStays()
    {
        AppSettings settings = sampleSettings();
        WritingProfileSettings standup;
        standup.profile = QStringLiteral("custom_standup");
        standup.name = QStringLiteral("Standup");
        settings.refinement.writingProfiles.append(standup);
        settings.learnedCorrections = {LearnedCorrection(), LearnedCorrection()};
        settings.output.pasteRules.append(
            {PasteRuleScope::Application, QStringLiteral("org.kde.konsole"),
             PasteMethod::TerminalPaste, true});

        const PhoneTransferText text = phoneTransferText(settings, PhoneTransferState::Waiting);
        QCOMPARE(text.included,
                 QStringList({QStringLiteral("1 vocabulary term, with its context and profiles"),
                              QStringLiteral("1 replacement or snippet"),
                              QStringLiteral("1 built-in Writing Profile and 1 of your own"),
                              QStringLiteral("1 tone of your own"),
                              QStringLiteral("1 cleanup level of your own"),
                              QStringLiteral("Additional instructions")}));
        QCOMPARE(text.stays, QStringList({QStringLiteral("2 learned corrections"),
                                          QStringLiteral("1 app-specific paste rule")}));
        QCOMPARE(text.staysHeading, QStringLiteral("Stays on this computer"));
        QVERIFY(phoneTransferText(AppSettings(), PhoneTransferState::Waiting).stays.isEmpty());
    }
};

int runPhoneTransferTests(int argc, char **argv)
{
    PhoneTransferTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_phone_transfer.moc"
