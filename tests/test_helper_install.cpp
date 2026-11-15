#include "common/test_suites.h"

#include "output/HelperInstall.h"
#include "setup/HelperCommands.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <pwd.h>
#include <unistd.h>

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

// Runs runSetupHelper with a pkexec that only records each command it is
// given, one line per call, and returns those lines.
QStringList setupHelperCommands(const char *installedHelperPath)
{
    QTemporaryDir bin;
    const QString log = bin.filePath(QStringLiteral("pkexec.log"));
    QFile pkexec(bin.filePath(QStringLiteral("pkexec")));
    if (!pkexec.open(QIODevice::WriteOnly)) {
        return {};
    }
    pkexec.write(QStringLiteral("#!/bin/sh\necho \"$*\" >> '%1'\n").arg(log).toUtf8());
    pkexec.close();
    pkexec.setPermissions(pkexec.permissions() | QFileDevice::ExeOwner);

    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(bin.path()) + ':' + path);
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", path); });
    QString error;
    if (!helpers::runSetupHelper(installedHelperPath, {}, helpers::HelperAction::Install, &error)) {
        return {error};
    }
    QFile output(log);
    if (!output.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(output.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
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

    // A `make install` into ~/.local puts the helper at its compiled-in path,
    // but the user owns it, so root runs only a root-owned copy of it.
    void aUserOwnedHelperAtItsInstalledPathIsStagedFirst()
    {
        QTemporaryDir directory;
        const QString helper = directory.filePath(QStringLiteral("speecher-test-setup"));
        QFile file(helper);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.close();
        QVERIFY(file.setPermissions(file.permissions() | QFileDevice::ExeOwner));

        const QStringList commands = setupHelperCommands(QFile::encodeName(helper).constData());
        const QString user = helpers::currentUserName();
        QCOMPARE(commands.size(), 2);
        QVERIFY2(commands.at(0).startsWith(QStringLiteral("/usr/bin/install -o root -g root")),
                 qPrintable(commands.at(0)));
        QCOMPARE(commands.at(1),
                 QStringLiteral("/usr/local/lib/speecher/setup/speecher-test-setup --install --user ")
                     + user);
    }

    // A helper that root alone can change, here a system binary, runs where it is.
    void aRootOwnedHelperRunsDirectly()
    {
        const QString helper =
            QFileInfo(QStandardPaths::findExecutable(QStringLiteral("true"))).canonicalFilePath();
        QVERIFY(!helper.isEmpty());

        const QStringList commands = setupHelperCommands(QFile::encodeName(helper).constData());
        QCOMPARE(commands,
                 QStringList{helper + QStringLiteral(" --install --user ")
                             + helpers::currentUserName()});
    }

    // Under pkexec a setup helper sets up the account that asked, and refuses
    // a --user naming any other.
    void aSetupHelperTakesTheAccountFromPkexec()
    {
        const std::string self = getpwuid(getuid())->pw_name;
        const std::string uid = std::to_string(getuid());
        std::string error;

        QCOMPARE(helpers::targetUser(uid.c_str(), {}, error), self);
        QCOMPARE(helpers::targetUser(uid.c_str(), self, error), self);
        QVERIFY(error.empty());
        QCOMPARE(helpers::targetUser(uid.c_str(), "someone-else", error), std::string());
        QVERIFY2(error.find("someone-else") != std::string::npos, error.c_str());
    }
};

#include "test_helper_install.moc"

int runHelperInstallTests(int argc, char **argv)
{
    HelperInstallTests tests;
    return runTestSuite(&tests, argc, argv);
}
