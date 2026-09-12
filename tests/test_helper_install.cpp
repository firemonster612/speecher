#include "common/test_suites.h"

#include "output/HelperInstall.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace speecher;

namespace {

// Talks like pkexec's textual agent: a banner, then a hidden password prompt
// on the controlling terminal, success only for the expected password. Echo
// goes off before the prompt is printed, as polkit's agent does. The runner
// gives the child a PTY, so stdin is that terminal.
constexpr auto agentScript = R"(#!/bin/sh
stty -echo
printf '==== AUTHENTICATING ====\nPassword: ' > /dev/tty
IFS= read -r password
stty echo
[ "$password" = "sesame" ] || { echo 'Not authorized' >&2; exit 127; }
exit 0
)";

// An identity choice first, with echo left on, then the password.
constexpr auto identityAgentScript = R"(#!/bin/sh
printf 'Choose identity to authenticate as (1-2): ' > /dev/tty
IFS= read -r identity
[ "$identity" = "1" ] || { echo 'No such identity' >&2; exit 127; }
stty -echo
printf 'Password: ' > /dev/tty
IFS= read -r password
stty echo
[ "$password" = "sesame" ] || { echo 'Not authorized' >&2; exit 127; }
exit 0
)";

QString writeScript(const QTemporaryDir &directory, const char *body)
{
    const QString path = directory.filePath(QStringLiteral("fake-pkexec"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(body);
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return path;
}

} // namespace

class HelperInstallTests final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { helpers::setPkexecPrompt({}); }

    void bridgesPasswordPromptToHandler()
    {
        QTemporaryDir directory;
        const QString script = writeScript(directory, agentScript);
        QVERIFY(!script.isEmpty());

        QString seenPrompt;
        bool seenEchoOff = false;
        helpers::setPkexecPrompt(
            [&](const QString &promptText, bool echoOff, QString *reply) {
                seenPrompt = promptText;
                seenEchoOff = echoOff;
                *reply = QStringLiteral("sesame");
                return true;
            });
        QString error;
        QVERIFY2(helpers::runPkexecConversation(script, {}, &error, 10000),
                 qPrintable(error));
        QVERIFY(seenPrompt.contains(QStringLiteral("Password:")));
        QVERIFY(seenEchoOff);
    }

    void wrongPasswordReportsTheProgramsError()
    {
        QTemporaryDir directory;
        const QString script = writeScript(directory, agentScript);
        QVERIFY(!script.isEmpty());

        helpers::setPkexecPrompt([](const QString &, bool, QString *reply) {
            *reply = QStringLiteral("wrong");
            return true;
        });
        QString error;
        QVERIFY(!helpers::runPkexecConversation(script, {}, &error, 10000));
        QCOMPARE(error, QStringLiteral("Not authorized"));
    }

    void answersIdentityChoiceThenPasswordWithoutEchoBleed()
    {
        QTemporaryDir directory;
        const QString script = writeScript(directory, identityAgentScript);
        QVERIFY(!script.isEmpty());

        QStringList prompts;
        helpers::setPkexecPrompt(
            [&](const QString &promptText, bool echoOff, QString *reply) {
                prompts.append(promptText);
                *reply = echoOff ? QStringLiteral("sesame") : QStringLiteral("1");
                return true;
            });
        QString error;
        QVERIFY2(helpers::runPkexecConversation(script, {}, &error, 10000),
                 qPrintable(error));
        QCOMPARE(prompts.size(), 2);
        QVERIFY(prompts.at(0).contains(QStringLiteral("Choose identity")));
        // The terminal's echo of the "1" reply is not the agent talking and
        // must not open the password prompt.
        QCOMPARE(prompts.at(1).trimmed(), QStringLiteral("Password:"));
    }

    void decliningThePromptCancels()
    {
        QTemporaryDir directory;
        const QString script = writeScript(directory, agentScript);
        QVERIFY(!script.isEmpty());

        helpers::setPkexecPrompt(
            [](const QString &, bool, QString *) { return false; });
        QString error;
        QVERIFY(!helpers::runPkexecConversation(script, {}, &error, 10000));
        QVERIFY2(error.contains(QStringLiteral("canceled")), qPrintable(error));
    }
};

#include "test_helper_install.moc"

int runHelperInstallTests(int argc, char **argv)
{
    HelperInstallTests tests;
    return runTestSuite(&tests, argc, argv);
}
