#include "common/test_suites.h"

#include "core/OutputMethod.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"
#include "output/TextDelivery.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "platform/win/WinTargetProvider.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <windows.h>

using namespace speecher;

namespace {

const SettingsRow &rowById(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return row;
            }
        }
    }
    qFatal("no row %s", qPrintable(id));
}

bool hasRow(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return true;
            }
        }
    }
    return false;
}

SchemaContext context()
{
    return {
        {{QStringLiteral("claude"), QStringLiteral("Claude Voice")}},
        {{QStringLiteral("openai"), QStringLiteral("OpenAI"), true}},
        [] { return QList<RowOption>{}; },
    };
}

} // namespace

class WinPlatformTests : public QObject {
    Q_OBJECT

private slots:
    void globalShortcutMappingRoundTrips()
    {
        const QList<QKeySequence> shortcuts{
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D),
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space),
            QKeySequence(Qt::ALT | Qt::Key_F5),
        };
        for (const QKeySequence &shortcut : shortcuts) {
            QString error;
            const auto hotKey = WinGlobalShortcutBinder::nativeHotKey(shortcut, &error);
            QVERIFY2(hotKey.has_value(), qPrintable(error));
            QCOMPARE(WinGlobalShortcutBinder::keySequenceForHotKey(
                         hotKey->modifiers, hotKey->virtualKey),
                     shortcut);
        }
    }

    void qtClipboardSnapshotRestoresFormatsWithoutAManifest()
    {
        auto *original = new QMimeData;
        original->setText(QStringLiteral("before"));
        original->setHtml(QStringLiteral("<b>before</b>"));
        original->setData(QStringLiteral("application/x-speecher-test"),
                          QByteArrayLiteral("private"));
        QApplication::clipboard()->setMimeData(original);

        ClipboardDelivery clipboard;
        ClipboardSnapshot snapshot;
        QString error;
        QVERIFY2(clipboard.capture(&snapshot, &error), qPrintable(error));
        QApplication::clipboard()->setText(QStringLiteral("after"));
        QVERIFY2(clipboard.restore(snapshot, &error), qPrintable(error));

        const QMimeData *restored = QApplication::clipboard()->mimeData();
        QCOMPARE(restored->text(), QStringLiteral("before"));
        QCOMPARE(restored->html(), QStringLiteral("<b>before</b>"));
        QCOMPARE(restored->data(QStringLiteral("application/x-speecher-test")),
                 QByteArrayLiteral("private"));
    }

    void schemaUsesWindowsCopyAndRows()
    {
        const SettingsSchema schema = buildSettingsSchema(context());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
        QVERIFY(hasRow(general, QStringLiteral("launchAtLogin")));
        QVERIFY(!hasRow(general, QStringLiteral("globalShortcut")));
        QVERIFY(!hasRow(general, QStringLiteral("removeSpeecher")));

        const SettingsRow &globalPaste = rowById(
            schema.page(QStringLiteral("output")), QStringLiteral("globalPasteRule"));
        const QList<RowOption> options = globalPaste.options(AppSettings{});
        QCOMPARE(options.at(0).label, QStringLiteral("Standard paste (Ctrl+V)"));
        QCOMPARE(options.at(1).label, QStringLiteral("Terminal paste (Ctrl+Shift+V)"));

        const SettingsRow &rules = rowById(
            schema.page(QStringLiteral("output")), QStringLiteral("applicationPasteRules"));
        QVERIFY(rules.help.contains(QStringLiteral("executable"), Qt::CaseInsensitive));
    }

    void liveHotkeyTargetAndDeliveryToNotepad()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_WIN_DESKTOP") != QStringLiteral("1")) {
            QSKIP("Live Windows desktop check is opt-in");
        }

        WinGlobalShortcutBinder shortcut;
        QString error;
        QVERIFY2(shortcut.setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D), &error),
                 qPrintable(error));
        QSignalSpy activated(&shortcut, &GlobalShortcutBinder::activated);
        shortcut.bind();

        INPUT input[6]{};
        const WORD keys[]{VK_CONTROL, VK_MENU, 'D', 'D', VK_MENU, VK_CONTROL};
        for (int index = 0; index < 6; ++index) {
            input[index].type = INPUT_KEYBOARD;
            input[index].ki.wVk = keys[index];
            if (index >= 3) {
                input[index].ki.dwFlags = KEYEVENTF_KEYUP;
            }
        }
        QCOMPARE(SendInput(6, input, sizeof(INPUT)), 6U);
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 2000);
        qInfo() << "windows live hotkey fired";

        WinTargetProvider targetProvider;
        const Target target = targetProvider.capture();
        qInfo().noquote() << QStringLiteral("windows live target process=%1 appId=%2 role=%3")
                                 .arg(target.processName, target.applicationId, target.role);
        QCOMPARE(target.processName.toLower(), QStringLiteral("notepad.exe"));
        QVERIFY(target.role.contains(QStringLiteral("document"), Qt::CaseInsensitive));

        OutputSettings output;
        output.method = QString::fromLatin1(OutputMethod::Automatic);
        output.restoreClipboardAfterTyping = false;
        output.pasteRules = {
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };
        const QString inserted = QStringLiteral("Speecher Windows delivery verified");
        const DeliveryResult result = TextDelivery(&targetProvider).deliver(
            output, makeDeliveryContent(inserted, OutputFormat::PlainText), target);
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.receipt == DeliveryReceipt::VerifiedInTarget
                || result.receipt == DeliveryReceipt::InputSent);
        if (result.receipt == DeliveryReceipt::InputSent) {
            QTest::qWait(500);
            QVERIFY(targetProvider.verifyInsertion(target, inserted));
        }
        qInfo().noquote() << "windows live delivery text=" + inserted;
    }
};

int runWinPlatformTests(int argc, char **argv)
{
    WinPlatformTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_win_platform.moc"
