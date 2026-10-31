#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "core/PasteRules.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QWheelEvent>

#include <memory>
#include <optional>

// The UI's QML modules are linked into the tests by QuickFrontEnd.cpp, which
// imports their plugins for the whole binary, as in the app.

using namespace speecher;

namespace {

// A SchemaRow for one schema row, or null with the reason in the log. The
// caller makes the Settings and Navigation it reads, as the host does.
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

// The items drawn under root of one type, a QML one such as "ChoiceCard" or
// a C++ class such as "QQuickText", in tree order. Walks the visual tree, since a Repeater's items are not
// root's QObject children.
QList<QQuickItem *> itemsOfType(QQuickItem *root, const char *type)
{
    const QByteArray prefix = QByteArray(type) + '_';
    QList<QQuickItem *> found;
    for (QQuickItem *item : root->childItems()) {
        if (QByteArray(item->metaObject()->className()).startsWith(prefix) || item->inherits(type)) {
            found.append(item);
        }
        found.append(itemsOfType(item, type));
    }
    return found;
}

// Cards of every kind ServiceCards shows: a long problem, a short status, no
// rating yet, and no rating at all.
constexpr const char *serviceCards = R"(
    import QtQuick
    import Speecher.Controls
    Window {
        width: 560; height: 700; visible: true
        ServiceCards {
            width: 520
            unratedText: "No rating"
            options: [
                { id: "local", label: "Local Runner", help: "Ollama, LM Studio or llama-server on this computer.",
                  status: "Could not connect to 127.0.0.1:11434: Connection refused", tone: "negative",
                  bars: [{ label: "Quality", value: 9 }, { label: "Speed", value: 7.5 }], note: "Rated as Gemma 4 E4B" },
                { id: "openai", label: "ChatGPT", help: "Uses your ChatGPT or Codex sign-in.", status: "Ready",
                  bars: [{ label: "Quality", value: 10 }, { label: "Speed", value: 7 }] },
                { id: "endpoint", label: "Custom Endpoint", help: "Your own server.", status: "No server URL is set",
                  tone: "warning", bars: null },
                { id: "none", label: "None", help: "Pastes your words as spoken." }
            ]
        }
    }
)";

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
        // A row that loads with a warning is a broken row.
        failOnUnexpectedWarnings();
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
        QCOMPARE(another.dictationSnapshot().ui.previewWords, 12);
        QCOMPARE(another.dictationSnapshot().ui.theme, QStringLiteral("dark"));
        QCOMPARE(another.dictationSnapshot().speech.fallbackProviderIds, QStringList{QStringLiteral("codex")});
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
        QCOMPARE(another.dictationSnapshot().shortcutActivationMode, ShortcutActivationMode::PushToTalk);
        QVERIFY(!another.dictationSnapshot().refinement.anthropicFastMode);
        QCOMPARE(another.dictationSnapshot().refinement.endpoint.preset, QStringLiteral("cliproxy"));
        QCOMPARE(another.dictationSnapshot().speech.local.idleUnloadMinutes, 60);

        settings.set(QStringLiteral("refinementEndpointServer"), QStringLiteral("custom"));
        QCOMPARE(SettingsStore().dictationSnapshot().refinement.endpoint.preset, QString());
        QCOMPARE(settings.values().value(QStringLiteral("refinementEndpointServer")).toString(), QStringLiteral("custom"));
        QCOMPARE(settings.values().value(QStringLiteral("anthropicFastMode")).toString(), QStringLiteral("standard"));
    }

    void updateIntervalIsListedOrCustom()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("1440"));
        QCOMPARE(SettingsStore().dictationSnapshot().updates.checkIntervalMinutes, 1440);
        QCOMPARE(settings.values().value(QStringLiteral("updateCheckInterval")).toString(), QStringLiteral("1440"));
        // What isn't a number of minutes saves nothing; a number past the
        // limits saves the nearest.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("updateCheckInterval\\) got no number")));
        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("hourly"));
        QCOMPARE(SettingsStore().dictationSnapshot().updates.checkIntervalMinutes, 1440);
        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("1"));
        QCOMPARE(SettingsStore().dictationSnapshot().updates.checkIntervalMinutes, UpdateSettings::minimumCheckIntervalMinutes);

        settings.set(QStringLiteral("updateCheckInterval"), QStringLiteral("custom"));
        settings.set(QStringLiteral("updateCheckCustomInterval"),
                     QVariantMap{{QStringLiteral("number"), 3}, {QStringLiteral("unit"), QStringLiteral("days")}});
        SettingsStore another;
        QCOMPARE(another.dictationSnapshot().updates.checkIntervalMinutes, 3 * 24 * 60);
        QCOMPARE(another.dictationSnapshot().updates.checkIntervalUnit, QStringLiteral("days"));
        QCOMPARE(settings.values().value(QStringLiteral("updateCheckInterval")).toString(), QStringLiteral("custom"));
        const QVariantMap custom = settings.values().value(QStringLiteral("updateCheckCustomInterval")).toMap();
        QCOMPARE(custom.value(QStringLiteral("number")).toInt(), 3);
        QCOMPARE(custom.value(QStringLiteral("unit")).toString(), QStringLiteral("days"));
    }

    void rowsNotSavedYetAreRefused()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);

        // Accounts saves the secrets.
        QTest::ignoreMessage(QtWarningMsg, "Settings.set: speechEndpointApiKey is not saved from the UI yet");
        settings.set(QStringLiteral("speechEndpointApiKey"), QStringLiteral("sk-made-up"));
        QVERIFY(!settings.values().contains(QStringLiteral("speechEndpointApiKey")));
    }

    // SchemaRow shows every row with a value as a setting, so each one has
    // to save. Custom rows draw their own content, secrets go to Accounts and
    // actions run.
    void everySchemaRowWithAValueSaves()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QQmlEngine engine;
        auto *schema = engine.singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        QVERIFY(schema);
        const QVariantList rows = schema->property("rows").value<QJSValue>().toVariant().toList();
        QVERIFY(!rows.isEmpty());
        for (const QVariant &entry : rows) {
            const QVariantMap row = entry.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            if (QStringList{QStringLiteral("custom"), QStringLiteral("secret"), QStringLiteral("action")}.contains(
                    row.value(QStringLiteral("kind")).toString())) {
                continue;
            }
            QVERIFY2(settings.values().contains(id), qPrintable(id));
        }
    }

    // Show the result for is core's completionStatusDurationMs: how long a
    // receipt stays up, any value core stores.
    void theResultDurationIsHowLongAReceiptStays()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QCOMPARE(settings.values().value(QStringLiteral("resultDuration")).toInt(), 500);

        settings.set(QStringLiteral("resultDuration"), 1500.0);
        QCOMPARE(SettingsStore().dictationSnapshot().output.completionStatusDurationMs, 1500);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const std::unique_ptr<QObject> duration = schemaRow(engine, "resultDuration");
        QVERIFY(duration->property("enabled").toBool());
        QVERIFY(duration->property("shown").toBool());
        const QObject *slider = nullptr;
        for (const QObject *child : duration->findChildren<QObject *>()) {
            if (QByteArray(child->metaObject()->className()).startsWith("Slider_")) {
                slider = child;
            }
        }
        QVERIFY(slider);
        QCOMPARE(slider->property("value").toInt(), 1500);
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
        LocalSpeechSettings local = controller.settings()->dictationSnapshot().speech.local;
        local.modelId = QStringLiteral("parakeet");
        controller.settings()->edit([&](AppSettings &draft) { draft.speech.local = local; return true; });
        emit controller.localSetup()->changed();
        QCOMPARE(settings.values().value(QStringLiteral("speechLocalModel")).toString(), QStringLiteral("parakeet"));
    }

    // SchemaRow decides in QML, so its binding follows Settings.values:
    // turning voice detection on enables its threshold with the page still up.
    void schemaRowsGreyOutUntilTurnedOn()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("vadEnabled"), false);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;

        const std::unique_ptr<QObject> threshold = schemaRow(engine, "vadThresholdPercent");
        QVERIFY(threshold);
        QVERIFY(!threshold->property("enabled").toBool());
        settings.set(QStringLiteral("vadEnabled"), true);
        QVERIFY(threshold->property("enabled").toBool());

        QVERIFY(schemaRow(engine, "pauseMedia")->property("enabled").toBool());
        // A custom row with a value is a setting; one without draws the page's own content.
        QVERIFY(schemaRow(engine, "defaultWritingProfile")->property("enabled").toBool());
        QVERIFY(schemaRow(engine, "themeControl")->property("enabled").toBool());
        QVERIFY(schemaRow(engine, "speechModels")->property("enabled").toBool());
    }

    // Paste rules are one list in core: each row edits its own rule and
    // keeps the others, and a row with no rule of its own inherits.
    void pasteRulesSaveByScope()
    {
        ApplicationController controller(true);
        // The page's app matches are Library's.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        // A copy, since the list it was found in goes with the lambda.
        const auto rule = [](PasteRuleScope scope, const char *match) -> std::optional<PasteRule> {
            const QList<PasteRule> rules = SettingsStore().dictationSnapshot().output.pasteRules;
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
        QCOMPARE(SettingsStore().dictationSnapshot().output.pasteRules.size(), 3);
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
        QuickNavigation navigation(nullptr);
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
        QCOMPARE(SettingsStore().dictationSnapshot().audio.preRollMs, slider->property("value").toInt());

        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(saved.count(), 2);
        QCOMPARE(SettingsStore().dictationSnapshot().audio.preRollMs, slider->property("value").toInt());
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
        QCOMPARE(another.dictationSnapshot().speech.fallbackProviderIds, QStringList{QStringLiteral("codex")});
        QCOMPARE(another.dictationSnapshot().audio.preRollMs, 300);
        QCOMPARE(another.dictationSnapshot().updates.checkIntervalMinutes, 120);
        QCOMPARE(another.dictationSnapshot().updates.checkIntervalUnit, QStringLiteral("hours"));
        QCOMPARE(result.toVariant().toStringList(), QStringList{QStringLiteral("codex")});
    }

    // A drag to either end holds the knob there, under the pointer, until it
    // is let go; then the switch flips to that end.
    void aSwitchKnobStaysWhereItIsDragged()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.Controls
            Window { width: 200; height: 80; visible: true; Switch { x: 40; y: 30 } }
        )", QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *control = itemsOfType(window->contentItem(), "Switch").value(0);
        QVERIFY(control);
        QQuickItem *track = control->property("indicator").value<QQuickItem *>();
        QQuickItem *knob = track->childItems().value(0);
        QVERIFY(knob);
        const qreal inset = knob->property("inset").toReal();
        const qreal middle = control->height() / 2;

        const auto drag = [&](qreal fromX, qreal toX) {
            const QPointF from = control->mapToScene(QPointF(fromX, middle));
            const QPointF to = control->mapToScene(QPointF(toX, middle));
            QTest::mousePress(window, Qt::LeftButton, {}, from.toPoint());
            for (int step = 1; step <= 10; ++step) {
                QTest::mouseMove(window, (from + (to - from) * step / 10).toPoint());
                QTest::qWait(16);
            }
            QTest::qWait(500);
            return to;
        };

        // Past the far end, still held.
        QPointF end = drag(inset + 4, control->width() + 30);
        QTRY_VERIFY2(qAbs(knob->x() + knob->width() - (track->width() - inset)) < 1,
                     qPrintable(QStringLiteral("knob ends at %1, not %2").arg(knob->x() + knob->width()).arg(track->width() - inset)));
        QVERIFY(!control->property("checked").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, {}, end.toPoint());
        QVERIFY(control->property("checked").toBool());

        // And back past the start.
        end = drag(control->width() - inset - 4, -30);
        QTRY_VERIFY2(qAbs(knob->x() - inset) < 1, qPrintable(QStringLiteral("knob starts at %1, not %2").arg(knob->x()).arg(inset)));
        QVERIFY(control->property("checked").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, {}, end.toPoint());
        QVERIFY(!control->property("checked").toBool());
    }

    // After something else moves the view, as a scroll bar drag or a jump
    // to a search result does, a wheel notch still glides it on from there.
    void theWheelScrollsOnAfterTheViewIsMovedElsewhere()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.Controls
            Window {
                property alias view: view
                property alias scroll: scroll
                width: 200; height: 200; visible: true
                Flickable { id: view; anchors.fill: parent; contentHeight: 2000; WheelScroll { id: scroll } }
            }
        )", QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *view = window->property("view").value<QObject *>();
        const QObject *scroll = window->property("scroll").value<QObject *>();
        const qreal notch = scroll->property("wheelLines").toReal() * scroll->property("lineHeight").toReal();
        const auto wheelDown = [window] {
            const QPointF at(100, 100);
            QWheelEvent event(at, window->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QGuiApplication::sendEvent(window, &event);
        };

        wheelDown();
        QTRY_COMPARE(view->property("contentY").toReal(), notch);
        view->setProperty("contentY", 500);
        wheelDown();
        QTRY_COMPARE(view->property("contentY").toReal(), 500 + notch);
    }

    // A quote in a link's address stays inside its href.
    void aQuoteInALinkStaysInItsHref()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\n"
                          "import Speecher.Controls\n"
                          "QtObject { readonly property string html: "
                          "RichText.inlineMarkdown('[notes](https://x.example/a\\\"b)', 'Mono', 'blue') }",
                          QUrl());
        std::unique_ptr<QObject> created(component.create());
        QVERIFY2(created, qPrintable(component.errorString()));
        QCOMPARE(created->property("html").toString(),
                 QStringLiteral("<a href=\"https://x.example/a&quot;b\" style=\"color: blue\">notes</a>"));
    }

    void aServiceCardsStatusStaysInItsCard_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("two wide columns") << 520;
        // The four take two columns of cards at their narrowest.
        QTest::newRow("two narrowest columns") << 400;
    }

    // However long a status is, it stays inside its card, above the ratings,
    // even where the cards are narrowest.
    void aServiceCardsStatusStaysInItsCard()
    {
        QFETCH(int, width);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(serviceCards, QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *grid = itemsOfType(window->contentItem(), "ServiceCards").value(0);
        QVERIFY(grid);
        grid->setWidth(width);
        QTest::qWait(500);

        const QList<QQuickItem *> cards = itemsOfType(window->contentItem(), "ChoiceCard");
        QCOMPARE(cards.size(), 4);
        for (QQuickItem *card : cards) {
            QQuickItem *status = itemsOfType(card, "StatusText").value(0);
            QQuickItem *bars = itemsOfType(card, "RatingBars").value(0);
            QVERIFY(status && bars);
            const QRectF statusRect = status->mapRectToItem(card, status->boundingRect());
            const QRectF barsRect = bars->mapRectToItem(card, bars->boundingRect());
            QVERIFY2(statusRect.bottom() <= barsRect.top(),
                     qPrintable(QStringLiteral("status ends at %1, ratings start at %2").arg(statusRect.bottom()).arg(barsRect.top())));
            QVERIFY(barsRect.bottom() <= card->height());
            for (QQuickItem *text : itemsOfType(status, "QQuickText")) {
                if (text->opacity() == 0 || text->property("text").toString().isEmpty()) {
                    continue;
                }
                const qreal right = text->mapToItem(card, QPointF(text->property("contentWidth").toReal(), 0)).x();
                QVERIFY2(right <= card->width(),
                         qPrintable(QStringLiteral("\"%1\" ends at %2 in a card %3 wide")
                                        .arg(text->property("text").toString()).arg(right).arg(card->width())));
            }
        }
    }

    // Assistive tech hears a card's ratings, or that it has none yet, as
    // well as its help and status.
    void aServiceCardSaysItsRatings()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(serviceCards, QUrl());
        std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));

        QStringList descriptions;
        for (QQuickItem *card : itemsOfType(window->contentItem(), "ChoiceCard")) {
            for (QObject *attached : card->findChildren<QObject *>(Qt::FindDirectChildrenOnly)) {
                if (attached->inherits("QQuickAccessibleAttached")) {
                    descriptions.append(attached->property("description").toString());
                }
            }
        }
        QCOMPARE(descriptions.size(), 4);
        QCOMPARE(descriptions[0], QStringLiteral("Ollama, LM Studio or llama-server on this computer. "
                                                 "Could not connect to 127.0.0.1:11434: Connection refused. "
                                                 "Quality 9 out of 10, Speed 7.5 out of 10. Rated as Gemma 4 E4B."));
        QCOMPARE(descriptions[1], QStringLiteral("Uses your ChatGPT or Codex sign-in. Ready. Quality 10 out of 10, Speed 7 out of 10."));
        QCOMPARE(descriptions[2], QStringLiteral("Your own server. No server URL is set. No rating."));
        QCOMPARE(descriptions[3], QStringLiteral("Pastes your words as spoken."));
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
