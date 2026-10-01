#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "frontend/win/CustomRows.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SettingsWindow.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SetupWindow.h"
#include "frontend/win/TranscribePane.h"
#include "frontend/win/TrayFlyout.h"
#include "frontend/win/WinFrontEnd.h"
#include "frontend/win/WinUiHost.h"
#include "ui/TranscriberPopup.h"

#include <windows.h>
#include <shellapi.h>

#include <QApplication>
#include <QTest>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <memory>

namespace speecher {
namespace {

bool nativeUiAvailable()
{
    return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

template<typename Widget>
int widgetCount()
{
    int count = 0;
    for (QWidget *widget : QApplication::allWidgets()) {
        count += dynamic_cast<Widget *>(widget) != nullptr;
    }
    return count;
}

} // namespace

class WinFrontEndTests : public QObject {
    Q_OBJECT

public:
    explicit WinFrontEndTests(std::unique_ptr<WinUiHost> host)
        : host(std::move(host))
    {
    }

private slots:
    void initTestCase()
    {
        SettingsStore settings;
        settings.raw().clear();
        if (qEnvironmentVariableIsSet("SPEECHER_TEST_PANEL_BANNERS")) {
            settings.setUpdatesPendingWhatsNewVersion(QStringLiteral("0.0.1"));
            // The offer stays an offer rather than a download.
            settings.setAutoInstallUpdates(false);
        }
        existingQtPopups = widgetCount<TranscriberPopup>();
        controller = std::make_unique<ApplicationController>(false);
        frontEnd = std::make_unique<WinFrontEnd>(controller.get(), std::move(host));
        controller->setFrontEnd(frontEnd.get());
        setup = std::make_unique<SetupWindow>(controller.get(), [] {});
    }

    void cleanupTestCase()
    {
        setup.reset();
        frontEnd.reset();
        controller.reset();
    }

    void retainedCollectionBaseline_data()
    {
        QTest::addColumn<bool>("scalarCommit");
        QTest::newRow("repeated collection saves") << false;
        QTest::newRow("scalar commit with retained editor") << true;
    }

    void retainedCollectionBaseline()
    {
        QFETCH(bool, scalarCommit);
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setLearnedCorrections({{"one", "githab", "GitHub", "editor", 100, 0.8, true, 1, 100}});
        win::SettingsModel model(controller.get());
        QList<QVariantMap> editorRecords;
        for (const SettingsPaneGroup &group : model.schema().pane(QStringLiteral("vocabulary"))->groups) {
            for (const auto &row : model.section(group).rows) {
                if (row.id == QStringLiteral("learnedCorrections")) {
                    editorRecords = row.value.value<QList<QVariantMap>>();
                }
            }
        }
        QCOMPARE(editorRecords.size(), 1);
        auto previous = editorRecords;
        auto fresh = store->learnedCorrections();
        fresh[0].evidenceCount = 3;
        fresh.append({"two", "new", "newer", "editor", 300, 0.9, true, 1, 300});
        store->setLearnedCorrections(fresh);
        if (scalarCommit) {
            model.setValue("previewWords", 12);
            model.commit();
        }
        for (bool enabled : {false, true}) {
            editorRecords[0].insert("enabled", enabled);
            QVERIFY(model.save(editorRecords, "learnedCorrections", previous).isEmpty());
            previous = editorRecords;
            const auto saved = store->learnedCorrections();
            QCOMPARE(saved.size(), 2);
            QCOMPARE(saved[0].evidenceCount, 3);
            QCOMPARE(saved[0].enabled, enabled);
            QCOMPARE(saved[1].id, QStringLiteral("two"));
        }
        if (scalarCommit) QCOMPARE(store->previewWords(), 12);
    }

    // The snippet column holds several lines, which the editor learns from
    // the column's flag rather than from the row's id.
    void snippetColumnIsMultiline()
    {
        win::SettingsModel model(controller.get());
        QStringList multiline;
        for (const SettingsPaneGroup &group : model.schema().pane(QStringLiteral("vocabulary"))->groups) {
            for (const auto &row : model.section(group).rows) {
                if (row.id != QStringLiteral("bindingRules")) {
                    continue;
                }
                for (const auto &column : row.collection->columns) {
                    if (column.multiline) {
                        multiline.append(column.id);
                    }
                }
            }
        }
        QCOMPARE(multiline, QStringList{QStringLiteral("replacement")});
    }

    // LocalSetup writes settings behind an open window (deleting the model in
    // use moves dictation to another); the draft takes that in without losing
    // an edit of its own.
    void settingsDraftTakesInStoreChangesAndKeepsItsEdits()
    {
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        LocalSpeechSettings local = store->localSpeechSettings();
        local.modelId = QStringLiteral("parakeet");
        store->setLocalSpeechSettings(local);
        win::SettingsModel model(controller.get());
        model.setValue(QStringLiteral("previewWords"), 12);

        local.modelId = QStringLiteral("moonshine-small");
        store->setLocalSpeechSettings(local);
        model.syncWithStore();

        QCOMPARE(model.draft().speech.local.modelId, QStringLiteral("moonshine-small"));
        QCOMPARE(model.draft().ui.previewWords, 12);
    }

    void constructionDoesNotCreateAQtDictationPopup()
    {
        QCOMPARE(widgetCount<TranscriberPopup>(), existingQtPopups);
    }

    void nativeDictationProblemCanBeDismissed()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showDictationError(QStringLiteral("The microphone stopped"));
        QVERIFY(frontEnd->panelVisibleForTest());

        frontEnd->dismissPanelForTest();
        QVERIFY(!frontEnd->panelVisibleForTest());
    }

    void nativeDictationPanelLevelMeterIsWaveformBars()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showPanelForTest(11);
        // Fifteen dots at the same thickness and spacing as the Linux waveform.
        QCOMPARE(frontEnd->dictationPanelForTest()->levelBarCountForTest(), 15);
        frontEnd->dismissPanelForTest();
    }

    void nativeDictationPanelSharesCapsuleAndClearsPreview()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        auto *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(13);
        QTRY_VERIFY(!panel->capsuleGeometryForTest().isEmpty());
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        const QRect compact = panel->capsuleGeometryForTest();
        panel->drivePreviewForTest(QStringLiteral("The meeting is on Thursday afternoon"));
        QTest::qWait(100);
        const QRect waveform = panel->waveformGeometryForTest();
        const QRect preview = panel->previewGeometryForTest();
        const QRect capsule = panel->capsuleGeometryForTest();
        QVERIFY(capsule.contains(waveform));
        QVERIFY(capsule.contains(preview));
        QVERIFY(preview.bottom() < waveform.top());
        QVERIFY(std::abs(preview.center().x() - waveform.center().x()) <= 1);
        QVERIFY(capsule.height() > compact.height());
        QCOMPARE(capsule.bottom(), compact.bottom());
        QVERIFY(capsule.width() > compact.width());
        controller->session()->popupFrozenChanged(true);
        panel->drivePreviewForTest(QStringLiteral("This preview must be ignored while frozen"));
        QCOMPARE(panel->previewGeometryForTest(), preview);
        controller->session()->popupFrozenChanged(false);
        panel->drivePreviewForTest(QString());
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        QCOMPARE(panel->capsuleGeometryForTest(), compact);
        panel->drivePreviewForTest(QStringLiteral("Clear this when audio stops"));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        controller->session()->popupRefiningChanged(true);
        controller->session()->popupRefinementPreviewChanged(QStringLiteral("Move the meeting to Thursday."));
        QTest::qWait(100);
        QVERIFY(panel->capsuleGeometryForTest().height() > compact.height());
        QCOMPARE(panel->capsuleGeometryForTest().bottom(), compact.bottom());
        controller->session()->popupOAuthRefreshRequested();
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        QVERIFY(panel->waveformGeometryForTest().isEmpty());
        QCOMPARE(panel->capsuleGeometryForTest().height(), compact.height());
        controller->session()->popupListeningIndicatorRequested();
        QTest::qWait(100);
        QCOMPARE(panel->capsuleGeometryForTest(), compact);
        QVERIFY(!panel->waveformGeometryForTest().isEmpty());
        controller->session()->popupFrozenChanged(false);
        panel->showForTest(14);
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        panel->drivePreviewForTest(QStringLiteral("A new Dictation Session"));
        QVERIFY(!panel->previewGeometryForTest().isEmpty());
        panel->dismissForTest();
        QVERIFY(panel->previewGeometryForTest().isEmpty());
    }

    void nativePreviewFitsTextAroundTheScreenCenter()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        auto *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(16);
        panel->drivePreviewForTest(QStringLiteral("Hello"));
        QTest::qWait(100);
        const QRect shortPreview = panel->capsuleGeometryForTest();
        panel->drivePreviewForTest(QStringLiteral("The meeting is on Thursday afternoon"));
        QTest::qWait(100);
        const QRect longerPreview = panel->capsuleGeometryForTest();
        QVERIFY(longerPreview.width() > shortPreview.width());
        QVERIFY(std::abs(longerPreview.center().x() - shortPreview.center().x()) <= 1);
        QCOMPARE(longerPreview.bottom(), shortPreview.bottom());
        panel->drivePreviewForTest(QString(100, QLatin1Char('W')));
        QTest::qWait(100);
        const QRect fullPreview = panel->capsuleGeometryForTest();
        QVERIFY(fullPreview.width() > longerPreview.width());
        for (const QString &ending : {QStringLiteral("i"), QStringLiteral("WW"), QStringLiteral(" thin words")}) {
            panel->drivePreviewForTest(QString(100, QLatin1Char('W')) + ending);
            QTest::qWait(50);
            QCOMPARE(panel->capsuleGeometryForTest(), fullPreview);
        }
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        controller->session()->popupRefiningChanged(true);
        controller->session()->popupRefinementPreviewChanged(QStringLiteral("Hello"));
        QTest::qWait(100);
        QCOMPARE(panel->capsuleGeometryForTest().width(), shortPreview.width());
        QVERIFY(std::abs(panel->capsuleGeometryForTest().center().x() - shortPreview.center().x()) <= 1);
        controller->session()->popupRefinementPreviewChanged(QString());
        QVERIFY(panel->previewGeometryForTest().isEmpty());
        panel->dismissForTest();
    }

    void nativePreviewKeepsNewestWordsWithinItsWidth_data()
    {
        QTest::addColumn<QString>("prefix");
        QTest::newRow("narrow-letters") << QString(300, QLatin1Char('i'));
        QTest::newRow("emoji-graphemes") << QString::fromUtf8("👩‍💻é").repeated(100);
    }

    void nativePreviewKeepsNewestWordsWithinItsWidth()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        QFETCH(QString, prefix);
        auto *panel = frontEnd->dictationPanelForTest();
        const QString newest = QString::fromUtf8(" Newest words 👩‍💻 arrive intact.");
        panel->showForTest(15);
        panel->drivePreviewForTest(prefix + newest);
        QTRY_VERIFY(panel->previewTextFitsForTest());
        const QString rendered = panel->previewTextForTest();
        QVERIFY(rendered.startsWith(QChar(0x2026)));
        QVERIFY(rendered.endsWith(newest));
        const QChar first = rendered.at(1);
        QVERIFY(!first.isLowSurrogate());
        QVERIFY(first != QChar(0x200d));
        QVERIFY(first.category() != QChar::Mark_NonSpacing);
        panel->dismissForTest();
    }

    void nativeDictationProblemAutoDismissesLikeTheQtPopup()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showDictationError(QStringLiteral("The microphone stopped"));
        QVERIFY(frontEnd->panelVisibleForTest());
        // The eight-second countdown every platform shows for a short error;
        // Dismiss stays the early exit.
        QTRY_VERIFY_WITH_TIMEOUT(!frontEnd->panelVisibleForTest(), 10000);
    }

    void panelEvidenceGrabsForDocumentation()
    {
        // Screenshot seam for UI evidence, on the pattern of the E2E rigs.
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (grabDir.isEmpty()) {
            QSKIP("SPEECHER_TEST_GRAB_DIR is not set");
        }
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(12);
        panel->driveStatusForTest(QStringLiteral("Listening"));
        panel->driveLevelForTest(0.02f);
        for (int i = 0; i < 40; ++i) {
            panel->driveLevelForTest(0.7f);
            QTest::qWait(24);
        }
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-listening.png")));
        controller->session()->popupFrozenChanged(true);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-frozen.png")));
        controller->session()->popupFrozenChanged(false);
        panel->drivePreviewForTest(QStringLiteral(
            "and then we should probably move the meeting to Thursday afternoon"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-preview.png")));
        panel->drivePreviewForTest(QStringLiteral("short preview"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-short-preview.png")));
        controller->session()->popupFrozenChanged(true);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-frozen-preview.png")));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-transcribing.png")));
        controller->session()->popupRefiningChanged(true);
        controller->session()->popupRefinementPreviewChanged(QStringLiteral("The meeting is on Thursday."));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-refining.png")));
        controller->session()->popupOAuthRefreshRequested();
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-renewal.png")));
        controller->session()->popupMessageRequested(QStringLiteral("Copied"), PopupOutcome::Copied);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-copied.png")));
        controller->session()->popupMessageRequested(QStringLiteral("Input sent"),
                                                     PopupOutcome::Inserted);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-inserted.png")));
        panel->dismissForTest();

        frontEnd->showDictationError(QStringLiteral("Microphone unavailable"));
        QTest::qWait(300);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-error-short.png")));
        panel->dismissForTest();
        frontEnd->showDictationError(QStringLiteral(
            "The transcription service rejected the request: the API key is invalid or has "
            "expired. Check the key on the Accounts page, then try again."),
            {ErrorFix::SettingsPage, QStringLiteral("accounts")});
        QTest::qWait(1500);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-error-long.png")));
        panel->dismissForTest();
        frontEnd->showDictationError(QStringLiteral("Could not reach https://example.com/")
                                     + QString(160, QLatin1Char('x')));
        QTest::qWait(1500);
        QVERIFY(panel->capsuleGeometryForTest().width()
                <= int((520 + 150) * GetDpiForSystem() / 96.0) + 2);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-error-unbroken.png")));
        panel->dismissForTest();

        // With SPEECHER_TEST_PANEL_BANNERS and an update manifest to offer,
        // both notices stack above the pill, each in its own capsule.
        if (qEnvironmentVariableIsSet("SPEECHER_TEST_PANEL_BANNERS")) {
            controller->updates()->checkForUpdates(controller->settings()->updateChannel());
            QTRY_VERIFY_WITH_TIMEOUT(
                controller->updates()->state() != UpdateController::State::Checking, 15000);
            panel->showForTest(13);
            panel->driveStatusForTest(QStringLiteral("Listening"));
            QTest::qWait(500);
            QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-banners.png")));
            panel->dismissForTest();
        }
    }

    // The flyout is as tall as what it holds, so a three-line transcript
    // leaves Settings inside it.
    void trayFlyoutGrowsWithItsTranscript()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        TrayFlyout *flyout = frontEnd->trayFlyoutForTest();
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const RECT icon{work.right - 60, work.bottom - 2, work.right - 40, work.bottom};
        flyout->show(icon);
        QTRY_VERIFY(!flyout->geometryForTest().isEmpty());
        const QRect empty = flyout->geometryForTest();

        emit controller->session()->previewChanged(QStringLiteral(
            "Can we move the design review to Thursday afternoon, and could you send the "
            "updated agenda to everyone on the list before the end of the day so people "
            "have time to read it and come with questions about the budget"));
        QTest::qWait(150);
        const QRect grown = flyout->geometryForTest();
        QVERIFY2(grown.height() > empty.height(), "the flyout kept its height for a transcript");
        QVERIFY(grown.contains(flyout->settingsGeometryForTest()));

        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            for (const QString theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
                SettingsStore settings;
                settings.setTheme(theme);
                flyout->hide();
                flyout->show(icon);
                QTest::qWait(300);
                QVERIFY(flyout->saveGrabForTest(
                    grabDir + QStringLiteral("/win-tray-flyout-%1.png").arg(theme)));
            }
            SettingsStore().setTheme(QStringLiteral("system"));
        }
        flyout->hide();
    }

    void nativeDictationPanelUsesNonActivatingTopmostToolWindowStyles()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showDictationError(QStringLiteral("Style probe"));
        const qintptr style = frontEnd->panelWindowStyleForTest();
        QVERIFY(style & WS_EX_NOACTIVATE);
        QVERIFY(style & WS_EX_TOOLWINDOW);
        QVERIFY(style & WS_EX_TOPMOST);
        frontEnd->dismissPanelForTest();
    }

    void popupPresentationAcknowledgesRequestedGeneration()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        constexpr quint64 generation = 73;
        frontEnd->showPanelForTest(generation);
        QTRY_COMPARE_WITH_TIMEOUT(frontEnd->panelPresentedGenerationForTest(), generation, 2000);
        frontEnd->dismissPanelForTest();
    }

    // Setup ends with a working Global Shortcut: one another app owns holds
    // the shortcut step and Finish, and says why.
    void setupHoldsFinishWhileTheShortcutIsTaken()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        // Another app, as Windows sees it: this thread taking the saved
        // combination while the binder has let go of it.
        QVERIFY(controller->setGlobalShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9)));
        controller->suspendGlobalShortcut();
        QVERIFY(RegisterHotKey(nullptr, 0x5ee7, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9));
        const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, 0x5ee7); });
        QVERIFY(!controller->resumeGlobalShortcut().isEmpty());
        setup->show(SetupAssistantPage::All);
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        const auto grab = [&](const QString &name) {
            if (!grabDir.isEmpty()) {
                QTest::qWait(400);
                QVERIFY(setup->captureForTest(grabDir + QStringLiteral("/win-setup-%1.png").arg(name)));
            }
        };
        grab(QStringLiteral("welcome"));
        setup->showPageForTest(QStringLiteral("shortcut"));
        QVERIFY(!setup->finishEnabledForTest());
        grab(QStringLiteral("shortcut-taken"));
        setup->showPageForTest(QStringLiteral("ready"));
        QVERIFY(!setup->finishEnabledForTest());
        grab(QStringLiteral("ready-blocked"));
        setup->skipForTest();
        QVERIFY(!controller->settings()->setupCompleted());
        QCOMPARE(setup->currentPageTitleForTest(), QStringLiteral("Global Shortcut"));
    }

    // Search offers each matching row with its pane, and says when nothing
    // matches rather than offering nothing.
    void settingsSearchSuggestsRows()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        win::TranscribePane transcribe(controller.get());
        win::SettingsWindow window(controller.get(), &transcribe);
        window.show();
        QVERIFY(window.searchSuggestionsForTest(QStringLiteral("Input device"))
                    .contains(QStringLiteral("dictation\naudioDevice")));
        QCOMPARE(window.searchSuggestionsForTest(QStringLiteral("zzqx")),
                 QStringList{QStringLiteral("No settings match")});
        window.close();
    }

    void skippingSetupOpensTheNativeSettingsWindow()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        setup->show(SetupAssistantPage::All);
        setup->skipForTest();

        QTRY_VERIFY_WITH_TIMEOUT(FindWindowW(nullptr, L"Speecher") != nullptr, 2000);
        QVERIFY(controller->settings()->setupCompleted());
        // Skip is Finish minus the pages in between: it has to leave a working
        // shortcut behind, or the app it completes cannot start dictation.
        QVERIFY(!controller->globalShortcut().isEmpty());
    }

    void openedAudioFilesOpenTheTranscribeWindowAlone()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        const bool settingsWasOpen = FindWindowW(nullptr, L"Speecher") != nullptr;
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("RIFF\0\0\0\0WAVEfmt ");
        file.close();

        frontEnd->showTranscribeFiles({audio});

        QTRY_VERIFY_WITH_TIMEOUT(FindWindowW(nullptr, L"Transcribe \u2014 Speecher") != nullptr, 2000);
        QCOMPARE(FindWindowW(nullptr, L"Speecher") != nullptr, settingsWasOpen);
    }

    void capabilitiesFollowWindowsAccessibility()
    {
        controller->frontEndReady();
        QTRY_VERIFY_WITH_TIMEOUT(controller->accessibilitySupported(), 2000);
        QVERIFY(controller->accessibilityEnabled());
        QVERIFY(controller->accessibilityPersistent());
    }

    void outputMethodsIncludeWindowsPaste()
    {
        const win::SettingsModel model(controller.get());
        const SettingsRow *row = model.schema().row(QStringLiteral("outputMethod"));
        QVERIFY(row);
        for (const RowOption &method : row->options(controller->settings()->snapshot())) {
            if (method.id == QString::fromLatin1(OutputMethod::WinPaste)) {
                QCOMPARE(method.label, QStringLiteral("Ctrl+V only"));
                return;
            }
        }
        QFAIL("Windows paste is missing from the output methods");
    }

    void authOptionLabelsAreReconciledWithW2()
    {
        const AppSettings draft = controller->settings()->snapshot();
        const auto labels = [](const QList<RowOption> &options) {
            QStringList result;
            for (const RowOption &option : options) {
                result.append(option.label);
            }
            return result;
        };
        QCOMPARE(labels(win::customRowOptions(QStringLiteral("openAiAuthMode"), draft,
                                              *controller->settings())),
                 QStringList({QStringLiteral("Automatic"),
                              QStringLiteral("API key from the Codex app"),
                              QStringLiteral("ChatGPT sign-in from the Codex app"),
                              QStringLiteral("API key from the environment"),
                              QStringLiteral("API key saved in Speecher"),
                              QStringLiteral("CLI Proxy API account")}));
        QCOMPARE(labels(win::customRowOptions(QStringLiteral("anthropicAuthMode"), draft,
                                              *controller->settings())),
                 QStringList({QStringLiteral("Claude Code sign-in"),
                              QStringLiteral("CLI Proxy API account")}));
    }

    void whatsNewOfferIsReconciledWithW2()
    {
        QVERIFY(win::SettingsWindow::offersWhatsNew(QStringLiteral("general"),
                                                    QStringLiteral("0.1.0")));
        QVERIFY(win::SettingsWindow::offersWhatsNew(QStringLiteral("whatsNew"), {}));
        QVERIFY(!win::SettingsWindow::offersWhatsNew(QStringLiteral("general"), {}));
    }

    void setupPageOrderAndCopyMatchTheWindowsFlow()
    {
        QCOMPARE(SetupWindow::pageTitles(),
                 QStringList({QStringLiteral("Welcome"),
                              QStringLiteral("Transcription"),
                              QStringLiteral("Microphone"),
                              QStringLiteral("Refinement"),
                              QStringLiteral("Global Shortcut"),
                              QStringLiteral("Ready to dictate")}));
        // No mention of desktop accessibility: this wizard has no such page.
        QVERIFY(!SetupWindow::welcomeCopyForTest().join(u' ').contains(QStringLiteral("ccessibility")));
        if (nativeUiAvailable()) {
            setup->show(SetupAssistantPage::GlobalShortcut);
            QCOMPARE(setup->currentPageTitleForTest(), QStringLiteral("Global Shortcut"));
        }
    }

    void panelVisualState()
    {
        const QString phase = qEnvironmentVariable("SPEECHER_WIN_PANEL_PHASE");
        if (phase.isEmpty()) {
            QSKIP("visual screenshot driver");
        }

        DictationSession *session = controller->session();
        if (phase == QStringLiteral("problem")) {
            frontEnd->showDictationError(QStringLiteral(
                "Microphone access is off. Check Windows privacy settings."));
        } else {
            frontEnd->showPanelForTest(91);
            session->popupStatusChanged(phase);
            session->previewDisplayChanged(
                QStringLiteral("A preview that grows while the user keeps dictating"));
            session->audioLevelChanged(0.65f);
            session->popupRefiningChanged(phase == QStringLiteral("Refining"));
        }
        QTest::qWait(15000);
        frontEnd->dismissPanelForTest();
    }

private:
    int existingQtPopups = 0;
    std::unique_ptr<WinUiHost> host;
    std::unique_ptr<ApplicationController> controller;
    std::unique_ptr<WinFrontEnd> frontEnd;
    std::unique_ptr<SetupWindow> setup;
};

} // namespace speecher

int runWinFrontEndTests(int argc, char **argv,
                        std::unique_ptr<speecher::WinUiHost> host)
{
    speecher::WinFrontEndTests tests(std::move(host));
    return runTestSuite(&tests, argc, argv);
}

#include "test_win_frontend.moc"
