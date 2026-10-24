#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QJSEngine>
#include <QSignalSpy>

using namespace speecher;

// The UI's Settings singleton: values by schema row id, saved through the
// daemon's store and read back by any other store, as the CLI's would be.
class QuickSettingsTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void setSavesThroughTheStore()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QSignalSpy changed(&settings, &QuickSettings::valuesChanged);

        settings.set(QStringLiteral("previewWords"), 12.0);
        settings.set(QStringLiteral("themeControl"), QStringLiteral("dark"));
        settings.set(QStringLiteral("speechFallbackList"), QVariantList{QStringLiteral("codex")});

        SettingsStore another;
        QCOMPARE(another.previewWords(), 12);
        QCOMPARE(another.theme(), QStringLiteral("dark"));
        QCOMPARE(another.speechFallbackProviders(), QStringList{QStringLiteral("codex")});
        QCOMPARE(settings.values().value(QStringLiteral("previewWords")).toInt(), 12);
        QCOMPARE(settings.values().value(QStringLiteral("themeControl")).toString(), QStringLiteral("dark"));
        QVERIFY(changed.count() >= 1);
    }

    void choicesUseTheSchemasOptionIds()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        settings.set(QStringLiteral("shortcutBehavior"), QStringLiteral("push_to_talk"));
        settings.set(QStringLiteral("anthropicFastMode"), QStringLiteral("standard"));
        settings.set(QStringLiteral("refinementEndpointServer"), QStringLiteral("cliproxy"));
        settings.set(QStringLiteral("localIdleUnload"), QStringLiteral("60"));

        SettingsStore another;
        QCOMPARE(another.shortcutActivationMode(), ShortcutActivationMode::PushToTalk);
        QVERIFY(!another.anthropicFastMode());
        QCOMPARE(another.refinementEndpointSettings().preset, QStringLiteral("cliproxy"));
        QCOMPARE(another.localSpeechSettings().idleUnloadMinutes, 60);

        settings.set(QStringLiteral("refinementEndpointServer"), QStringLiteral("custom"));
        QCOMPARE(SettingsStore().refinementEndpointSettings().preset, QString());
        QCOMPARE(settings.values().value(QStringLiteral("refinementEndpointServer")).toString(), QStringLiteral("custom"));
        QCOMPARE(settings.values().value(QStringLiteral("anthropicFastMode")).toString(), QStringLiteral("standard"));
    }

    void updateIntervalIsListedOrCustom()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("1440"));
        QCOMPARE(SettingsStore().updateCheckIntervalMinutes(), 1440);
        QCOMPARE(settings.values().value(QStringLiteral("updateCheckInterval")).toString(), QStringLiteral("1440"));

        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("custom"));
        settings.set(QStringLiteral("updateCheckCustomInterval"),
                     QVariantMap{{QStringLiteral("number"), 3}, {QStringLiteral("unit"), QStringLiteral("days")}});
        SettingsStore another;
        QCOMPARE(another.updateCheckIntervalMinutes(), 3 * 24 * 60);
        QCOMPARE(another.updateCheckIntervalUnit(), QStringLiteral("days"));
        QCOMPARE(settings.values().value(QStringLiteral("updateCheckInterval")).toString(), QStringLiteral("custom"));
        const QVariantMap custom = settings.values().value(QStringLiteral("updateCheckCustomInterval")).toMap();
        QCOMPARE(custom.value(QStringLiteral("number")).toInt(), 3);
        QCOMPARE(custom.value(QStringLiteral("unit")).toString(), QStringLiteral("days"));
    }

    void rowsNotSavedYetAreRefusedAndGreyedOut()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        QTest::ignoreMessage(QtWarningMsg, "Settings.set: globalPasteRule is not saved from the UI yet");
        settings.set(QStringLiteral("globalPasteRule"), QStringLiteral("clipboard"));
        QVERIFY(!settings.values().contains(QStringLiteral("globalPasteRule")));

        const auto row = [](const char *id, const char *kind) {
            return QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                               {QStringLiteral("kind"), QString::fromLatin1(kind)}};
        };
        QVERIFY(!settings.rowEnabled(row("globalPasteRule", "choice")));
        QVERIFY(!settings.rowEnabled(row("speechEndpointApiKey", "secret")));
        QVERIFY(!settings.rowEnabled(row("globalShortcut", "shortcut")));
        QVERIFY(settings.rowEnabled(row("pauseMedia", "toggle")));
        QVERIFY(settings.rowEnabled(row("runSetup", "action")));
        QVERIFY(settings.rowEnabled(row("speechModels", "custom")));
    }

    void enabledWhenFollowsTheValues()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QVariantMap threshold{{QStringLiteral("id"), QStringLiteral("vadThresholdPercent")},
                              {QStringLiteral("kind"), QStringLiteral("slider")},
                              {QStringLiteral("enabledWhen"),
                               QVariantMap{{QStringLiteral("id"), QStringLiteral("vadEnabled")},
                                           {QStringLiteral("equals"), true}}}};

        settings.set(QStringLiteral("vadEnabled"), false);
        QVERIFY(!settings.rowEnabled(threshold));
        settings.set(QStringLiteral("vadEnabled"), true);
        QVERIFY(settings.rowEnabled(threshold));
    }

    void changesSavedElsewhereShowUp()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QSignalSpy changed(&settings, &QuickSettings::valuesChanged);

        SettingsStore *store = controller.settings();
        AppSettings draft = store->dictationSnapshot();
        draft.ui.soundsEnabled = true;
        store->applySnapshot(draft);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(settings.values().value(QStringLiteral("playSounds")).toBool(), true);
    }

    // What a page hands over: JavaScript arrays, numbers and objects.
    void javaScriptSetsWithItsOwnValues()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QJSEngine engine;
        QJSEngine::setObjectOwnership(&settings, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("Settings"), engine.newQObject(&settings));

        const QJSValue result = engine.evaluate(QStringLiteral(R"(
            Settings.set("speechFallbackList", ["codex"])
            Settings.set("preRollMs", 300)
            Settings.set("updateCheckInterval", "custom")
            Settings.set("updateCheckCustomInterval", { number: 2, unit: "hours" })
            Settings.values.speechFallbackList
        )"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));

        SettingsStore another;
        QCOMPARE(another.speechFallbackProviders(), QStringList{QStringLiteral("codex")});
        QCOMPARE(another.audioPreRollMs(), 300);
        QCOMPARE(another.updateCheckIntervalMinutes(), 120);
        QCOMPARE(another.updateCheckIntervalUnit(), QStringLiteral("hours"));
        QCOMPARE(result.toVariant().toStringList(), QStringList{QStringLiteral("codex")});
    }

    void shortcutKeysSplitIntoKeycaps()
    {
        QCOMPARE(shortcutKeys(ShortcutBinding(QKeySequence(QStringLiteral("Ctrl+Space")))),
                 (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Space")}));
        QCOMPARE(shortcutKeys(ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_Plus))),
                 (QStringList{QStringLiteral("Ctrl"), QStringLiteral("+")}));
        QCOMPARE(shortcutKeys(ShortcutBinding()), QStringList());
    }
};

int runQuickSettingsTests(int argc, char **argv)
{
    QuickSettingsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_settings.moc"
