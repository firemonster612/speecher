#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "core/PasteRules.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

#include <memory>
#include <optional>

// The UI's QML modules are linked into the tests by QuickFrontEnd.cpp, which
// imports their plugins for the whole binary, as in the app.

using namespace speecher;

namespace {

// A SchemaRow for one schema row, or null with the reason in the log.
std::unique_ptr<QObject> schemaRow(QQmlEngine &engine, const char *rowId)
{
    QQmlComponent component(&engine);
    component.setData(QStringLiteral("import Speecher.App\nSchemaRow { rowId: \"%1\" }").arg(QLatin1String(rowId)).toUtf8(),
                      QUrl());
    std::unique_ptr<QObject> row(component.create());
    if (!row) {
        qWarning().noquote() << component.errorString();
    }
    return row;
}

} // namespace

// The UI's Settings singleton: values by schema row id, saved through the
// daemon's store and read back by any other store, as the CLI's would be.
class QuickSettingsTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
        // A row that loads with a warning, such as an absent value assigned to
        // a string, is a broken row.
        QTest::failOnWarning();
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

    void rowsNotSavedYetAreRefused()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        QTest::ignoreMessage(QtWarningMsg, "Settings.set: speechEndpointApiKey is not saved from the UI yet");
        settings.set(QStringLiteral("speechEndpointApiKey"), QStringLiteral("sk-made-up"));
        QVERIFY(!settings.values().contains(QStringLiteral("speechEndpointApiKey")));

        // A placeholder area's made-up choice never reaches the config.
        const QString profile = SettingsStore().dictationSnapshot().refinement.defaultWritingProfile;
        QTest::ignoreMessage(QtWarningMsg, "Settings.set: defaultWritingProfile is not saved from the UI yet");
        settings.set(QStringLiteral("defaultWritingProfile"), QStringLiteral("made-up"));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.defaultWritingProfile, profile);

        // Accounts saves the secrets.
        for (const char *id : {"speechEndpointApiKey", "defaultWritingProfile"}) {
            QVERIFY2(!settings.saves(QLatin1String(id)), id);
        }
        for (const char *id : {"pauseMedia", "audioDevice", "globalShortcut", "cancelShortcut", "pauseShortcut", "runSetup",
                               "globalPasteRule", "categoryPasteRule_ai_coding", "applicationPasteRules",
                               "speechLocalModel", "localRunner", "localAcceleration", "localGraphicsCard",
                               "openAiCliproxyAccount", "anthropicCliproxyAccount"}) {
            QVERIFY2(settings.saves(QLatin1String(id)), id);
        }
    }

    // The Local Model is a choice once picked; a new acceleration starts on
    // its first card here, the one the engine runs it on, until the card row
    // picks another, and the CPU names none; the runner is the one named.
    void localModelRowsSaveWhereModelsRun()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        const QString first = QStringLiteral("0000:01:00.0");
        const QString second = QStringLiteral("0000:02:00.0");
        HardwareSummary hardware;
        for (const QString &id : {first, second}) {
            LocalSpeechEngine::Device card;
            card.id = id;
            card.kind = QStringLiteral("vulkan");
            card.type = LocalSpeechEngine::Device::Type::Gpu;
            hardware.gpus.append(card);
        }
        LocalSetupTestAccess::setHardware(*controller.localSetup(), hardware);

        settings.set(QStringLiteral("speechLocalModel"), QStringLiteral("qwen3-asr"));
        settings.set(QStringLiteral("localAcceleration"), QStringLiteral("vulkan"));
        settings.set(QStringLiteral("localRunner"), QStringLiteral("llama-server"));
        AppSettings saved = SettingsStore().dictationSnapshot();
        QCOMPARE(saved.speech.local.modelId, QStringLiteral("qwen3-asr"));
        QVERIFY(saved.speech.local.modelChosen);
        QCOMPARE(saved.speech.local.runsOn, (LocalRunsOn{QStringLiteral("vulkan"), first}));
        QCOMPARE(saved.refinement.localRunner.runner, QStringLiteral("llama-server"));

        settings.set(QStringLiteral("localGraphicsCard"), second);
        saved = SettingsStore().dictationSnapshot();
        QCOMPARE(saved.speech.local.runsOn, (LocalRunsOn{QStringLiteral("vulkan"), second}));
        QCOMPARE(settings.values().value(QStringLiteral("localAcceleration")).toString(), QStringLiteral("vulkan"));
        QCOMPARE(settings.values().value(QStringLiteral("localGraphicsCard")).toString(), second);
        settings.set(QStringLiteral("localAcceleration"), QStringLiteral("cpu"));
        QCOMPARE(SettingsStore().dictationSnapshot().speech.local.runsOn, (LocalRunsOn{QStringLiteral("cpu"), QString()}));

        // LocalSetup saves a model it moves dictation to without a snapshot,
        // as deleting the one in use does, and the row follows its news.
        LocalSpeechSettings local = controller.settings()->localSpeechSettings();
        local.modelId = QStringLiteral("parakeet");
        controller.settings()->setLocalSpeechSettings(local);
        emit controller.localSetup()->changed();
        QCOMPARE(settings.values().value(QStringLiteral("speechLocalModel")).toString(), QStringLiteral("parakeet"));
    }

    // SchemaRow decides in QML, so its binding follows Settings.values:
    // turning voice detection on enables its threshold with the page still up.
    void schemaRowsGreyOutWhatIsNotSaved()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("vadEnabled"), false);
        QQmlEngine engine;

        const std::unique_ptr<QObject> threshold = schemaRow(engine, "vadThresholdPercent");
        QVERIFY(threshold);
        QVERIFY(!threshold->property("enabled").toBool());
        settings.set(QStringLiteral("vadEnabled"), true);
        QVERIFY(threshold->property("enabled").toBool());

        QVERIFY(schemaRow(engine, "pauseMedia")->property("enabled").toBool());
        // A greyed choice the backend does not save shows no choice at all.
        const std::unique_ptr<QObject> duration = schemaRow(engine, "resultDuration");
        QVERIFY(!duration->property("enabled").toBool());
        const QObject *select = nullptr;
        for (const QObject *child : duration->findChildren<QObject *>()) {
            if (QByteArray(child->metaObject()->className()).startsWith("Select_")) {
                select = child;
            }
        }
        QVERIFY(select);
        QCOMPARE(select->property("currentIndex").toInt(), -1);
        // A custom row with a value is a setting; one without draws the page's own content.
        QVERIFY(!schemaRow(engine, "defaultWritingProfile")->property("enabled").toBool());
        QVERIFY(schemaRow(engine, "themeControl")->property("enabled").toBool());
        QVERIFY(schemaRow(engine, "speechModels")->property("enabled").toBool());
    }

    // Paste rules are one list in core: each row edits its own rule and
    // keeps the others, and a row with no rule of its own inherits.
    void pasteRulesSaveByScope()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        // A copy, since the list it was found in goes with the lambda.
        const auto rule = [](PasteRuleScope scope, const char *match) -> std::optional<PasteRule> {
            const QList<PasteRule> rules = SettingsStore().pasteRules();
            const PasteRule *found = findPasteRule(rules, scope, QLatin1String(match));
            return found ? std::optional(*found) : std::nullopt;
        };
        const auto value = [&settings](const char *id) { return settings.values().value(QLatin1String(id)); };

        QCOMPARE(value("categoryPasteRule_terminal").toString(), QStringLiteral("terminal_paste"));
        QCOMPARE(value("categoryPasteRule_browser").toString(), QStringLiteral("inherit"));
        settings.set(QStringLiteral("globalPasteRule"), QStringLiteral("clipboard_only"));
        settings.set(QStringLiteral("categoryPasteRule_browser"), QStringLiteral("direct_insert"));
        settings.set(QStringLiteral("categoryPasteRule_terminal"), QStringLiteral("inherit"));
        QCOMPARE(rule(PasteRuleScope::Global, "")->method, PasteMethod::ClipboardOnly);
        QCOMPARE(rule(PasteRuleScope::Category, "browser")->method, PasteMethod::DirectInsert);
        QVERIFY(!rule(PasteRuleScope::Category, "terminal"));
        QCOMPARE(value("categoryPasteRule_terminal").toString(), QStringLiteral("inherit"));
        QCOMPARE(value("categoryPasteRule_browser").toString(), QStringLiteral("direct_insert"));

        const QVariantList apps{
            QVariantMap{{QStringLiteral("app"), QStringLiteral("org.kde.konsole")},
                        {QStringLiteral("method"), QStringLiteral("standard_paste")},
                        {QStringLiteral("enabled"), false}},
        };
        settings.set(QStringLiteral("applicationPasteRules"), apps);
        QCOMPARE(*rule(PasteRuleScope::Application, "org.kde.konsole"),
                 (PasteRule{PasteRuleScope::Application, QStringLiteral("org.kde.konsole"), PasteMethod::StandardPaste, false}));
        QCOMPARE(rule(PasteRuleScope::Category, "browser")->method, PasteMethod::DirectInsert);
        QCOMPARE(value("applicationPasteRules").toList(), apps);
        QVERIFY(!settings.errors().contains(QStringLiteral("applicationPasteRules")));

        // Two rules for one app are refused with core's reason, and nothing is saved.
        settings.set(QStringLiteral("applicationPasteRules"), apps + apps);
        QVERIFY(!settings.errors().value(QStringLiteral("applicationPasteRules")).toString().isEmpty());
        QCOMPARE(SettingsStore().pasteRules().size(), 3);
        settings.set(QStringLiteral("applicationPasteRules"), QVariantList{});
        QVERIFY(!rule(PasteRuleScope::Application, "org.kde.konsole"));
        QVERIFY(!settings.errors().contains(QStringLiteral("applicationPasteRules")));
    }

    // Each save is a whole snapshot through the store, so a drag saves once,
    // when it lets go; a key press saves its one step.
    void aSliderSavesWhenLetGo()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("preRollMs"), 0);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.App
            Window {
                width: 640; height: 120; visible: true
                SchemaRow { rowId: "preRollMs"; width: parent.width }
            }
        )", QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *slider = nullptr;
        for (QQuickItem *item : window->findChildren<QQuickItem *>()) {
            if (QByteArray(item->metaObject()->className()).startsWith("Slider_")) {
                slider = item;
            }
        }
        QVERIFY(slider);
        QSignalSpy saved(controller.settings(), &SettingsStore::snapshotApplied);

        const QPointF start = slider->mapToScene(QPointF(2, slider->height() / 2));
        const QPointF end = slider->mapToScene(QPointF(slider->width() * 0.8, slider->height() / 2));
        QTest::mousePress(window, Qt::LeftButton, {}, start.toPoint());
        for (int step = 1; step <= 8; ++step) {
            QTest::mouseMove(window, (start + (end - start) * step / 8).toPoint());
        }
        QVERIFY(slider->property("value").toInt() > 0);
        QCOMPARE(saved.count(), 0);
        QTest::mouseRelease(window, Qt::LeftButton, {}, end.toPoint());
        QCOMPARE(saved.count(), 1);
        QCOMPARE(SettingsStore().audioPreRollMs(), slider->property("value").toInt());

        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(saved.count(), 2);
        QCOMPARE(SettingsStore().audioPreRollMs(), slider->property("value").toInt());
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
        QCOMPARE(shortcutKeys(QStringLiteral("Ctrl+Space")), (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Space")}));
        QCOMPARE(shortcutKeys(QStringLiteral("Ctrl++")), (QStringList{QStringLiteral("Ctrl"), QStringLiteral("+")}));
        QCOMPARE(shortcutKeys(QStringLiteral("Numpad +")), QStringList{QStringLiteral("Numpad +")});
        QCOMPARE(shortcutKeys(QString()), QStringList());
    }
};

int runQuickSettingsTests(int argc, char **argv)
{
    QuickSettingsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_settings.moc"
