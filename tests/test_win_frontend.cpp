#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "core/settings/FallbackPresentation.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupGeometry.h"
#include "dictation/PopupPresentation.h"
#include "frontend/win/CustomRows.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SettingsWindow.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SetupWindow.h"
#include "frontend/win/TranscribePane.h"
#include "frontend/win/TrayFlyout.h"
#include "frontend/win/WaveformBars.h"
#include "frontend/win/WinFrontEnd.h"
#include "frontend/win/WinUiHost.h"
#include "providers/ProviderRegistry.h"
#include "ui/TranscriberPopup.h"

#include <windows.h>
#include <shellapi.h>

#include <QApplication>
#include <QDebug>
#include <QTest>
#include <QFile>
#include <QScopeGuard>
#include <QStringList>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

namespace speecher {
namespace {

bool nativeUiAvailable()
{
    return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

// DIPs to the physical pixels the panel's geometry hooks report.
double dipScale()
{
    return GetDpiForSystem() / 96.0;
}

// The boxes a layout check compared, for its failure message.
QString describeBoxes(const QList<QRect> &boxes)
{
    QString text;
    QDebug(&text) << boxes;
    return text;
}

// A provider's registry label, as the Add a fallback combo lists it.
QString chainLabel(ProviderRole role, const ProviderRegistry &registry, const QString &id)
{
    for (const RowOption &provider : win::chainProviders(role, registry)) {
        if (provider.id == id) {
            return provider.label;
        }
    }
    return id;
}

// Saves a refinement provider as a choice the person made, which the setup
// assistant never replaces. Saving the default over an unset one leaves it a
// default, and the default depends on which CLIs this computer has.
void chooseRefinementProvider(SettingsStore &store, const QString &id)
{
    store.setRefinementProvider(id == QStringLiteral("openai") ? QStringLiteral("anthropic")
                                                               : QStringLiteral("openai"));
    store.setRefinementProvider(id);
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
        // The popup's nine fine dots; Home keeps the Linux waveform's fifteen.
        QCOMPARE(frontEnd->dictationPanelForTest()->levelBarCountForTest(), 9);
        frontEnd->dismissPanelForTest();
        QCOMPARE(win::WaveformBars().count(), 15);
    }

    // The slim capsule: a 32 pill hugging [pause] 8 [dots] 8 [X] with 6 of
    // lobe either end.
    void nativeDictationPanelIsTheSlimPill()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        auto *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(17);
        controller->session()->stateChanged(QStringLiteral("listening"));
        QTest::qWait(100);
        const QRect capsule = panel->capsuleGeometryForTest();
        // 6 + 24 + 8 + (9 dots 2 wide, 2 apart) + 8 + 24 + 6.
        QVERIFY(std::abs(capsule.width() - 110 * dipScale()) <= 1);
        QVERIFY(std::abs(capsule.height() - 32 * dipScale()) <= 1);
        QVERIFY(std::abs(panel->cancelGeometryForTest().width() - 24 * dipScale()) <= 1);
        controller->session()->stateChanged(QStringLiteral("idle"));
        panel->dismissForTest();
    }

    // A preview shorter than the lobe's carving still sits in a text bar over
    // the lobe, centred in the narrowest bar that carves it, on one axis with
    // the row under it: [pause] 8 [dots] 8 [X].
    void nativeShortPreviewKeepsTheCarvedContour()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        auto *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(18);
        controller->session()->stateChanged(QStringLiteral("listening"));
        panel->drivePreviewForTest(QStringLiteral("Hi"));
        QRect capsule;
        QRect preview;
        QRect pause;
        QRect dots;
        QRect cancel;
        const auto centred = [&] {
            capsule = panel->capsuleGeometryForTest();
            preview = panel->previewGeometryForTest();
            pause = panel->pauseGeometryForTest();
            dots = panel->waveformGeometryForTest();
            cancel = panel->cancelGeometryForTest();
            return std::abs(preview.center().x() - capsule.center().x()) <= 1
                && std::abs(dots.center().x() - capsule.center().x()) <= 1
                && std::abs((dots.left() - pause.right()) - (cancel.left() - dots.right())) <= 1;
        };
        QTRY_VERIFY2(centred(), qPrintable(describeBoxes({capsule, preview, pause, dots, cancel})));
        QVERIFY(std::abs((dots.left() - pause.right()) - 8 * dipScale()) <= 2);
        const double shoulder = panel->outlineShoulderForTest();
        QVERIFY2(shoulder > 0, "a short preview collapsed into a plain rounded box");
        QCOMPARE(panel->outlineLobeWidthForTest(), 110.0);
        QVERIFY(capsule.width() / dipScale() + 1 >= popup::minimumPreviewBarWidth(110, shoulder));
        controller->session()->stateChanged(QStringLiteral("idle"));
        panel->dismissForTest();
    }

    // While refining a spinner takes pause's slot and the label hugs its
    // text, so [spinner] 8 [label] 8 [X] is one row centred in the capsule.
    void nativeRefiningCentresItsLabelBetweenSpinnerAndCancel_data()
    {
        QTest::addColumn<QString>("refinedText");
        QTest::newRow("no text") << QString();
        QTest::newRow("with text") << QStringLiteral("Move the meeting to Thursday.");
    }

    void nativeRefiningCentresItsLabelBetweenSpinnerAndCancel()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        QFETCH(QString, refinedText);
        auto *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(19);
        controller->session()->stateChanged(QStringLiteral("refining"));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        controller->session()->popupRefiningChanged(true);
        controller->session()->popupRefinementPreviewChanged(refinedText);
        QRect capsule;
        QRect spinner;
        QRect label;
        QRect cancel;
        const auto centred = [&] {
            capsule = panel->capsuleGeometryForTest();
            spinner = panel->spinnerGeometryForTest();
            label = panel->statusGeometryForTest();
            cancel = panel->cancelGeometryForTest();
            return !spinner.isEmpty() && !cancel.isEmpty()
                && std::abs(label.center().x() - capsule.center().x()) <= 1
                && std::abs((label.left() - spinner.right()) - (cancel.left() - label.right())) <= 1;
        };
        QTRY_VERIFY2(centred(), qPrintable(describeBoxes({capsule, spinner, label, cancel})));
        QCOMPARE(spinner.width(), cancel.width());
        QVERIFY(std::abs((label.left() - spinner.right()) - 8 * dipScale()) <= 2);
        if (refinedText.isEmpty()) {
            QVERIFY(std::abs((spinner.left() - capsule.left()) - 6 * dipScale()) <= 2);
            QVERIFY(std::abs((capsule.right() - cancel.right()) - 6 * dipScale()) <= 2);
        }
        controller->session()->popupRefiningChanged(false);
        controller->session()->stateChanged(QStringLiteral("idle"));
        panel->dismissForTest();
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
        // The refined words hug their text too, in the narrowest bar that
        // still carves around the status row beneath them.
        QVERIFY(panel->capsuleGeometryForTest().width() < longerPreview.width());
        QVERIFY(panel->outlineShoulderForTest() > 0);
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
        // Cut words fade out on the left rather than giving way to an ellipsis.
        QVERIFY(!rendered.startsWith(QChar(0x2026)));
        QVERIFY(panel->previewFadesForTest());
        QVERIFY(rendered.endsWith(newest.trimmed()));
        const QChar first = rendered.at(0);
        QVERIFY(!first.isLowSurrogate());
        QVERIFY(first != QChar(0x200d));
        QVERIFY(first.category() != QChar::Mark_NonSpacing);
        // A preview that fits whole does not fade.
        panel->drivePreviewForTest(newest);
        QTRY_COMPARE(panel->previewTextForTest(), newest.simplified());
        QVERIFY(!panel->previewFadesForTest());
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
        // The pause and cancel buttons either side of the waveform, and Paused.
        controller->session()->stateChanged(QStringLiteral("listening"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-listening-controls.png")));
        controller->session()->stateChanged(QStringLiteral("paused"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-paused.png")));
        controller->session()->stateChanged(QStringLiteral("listening"));
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
        controller->session()->stateChanged(QStringLiteral("stopping"));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-transcribing.png")));
        controller->session()->stateChanged(QStringLiteral("refining"));
        controller->session()->popupRefiningChanged(true);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-refining-empty.png")));
        controller->session()->popupRefinementPreviewChanged(QStringLiteral("The meeting is on Thursday."));
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-refining.png")));
        controller->session()->popupOAuthRefreshRequested();
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-renewal.png")));
        controller->session()->popupMessageRequested(QStringLiteral("Copied"), PopupOutcome::Copied);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-copied.png")));
        controller->session()->popupMessageRequested(cancelledOutcomeText(), PopupOutcome::Cancelled);
        QTest::qWait(150);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-canceled.png")));
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

    // The Fallbacks row names the subpage it opens, and the list row takes a
    // whole new list through the ordinary write path.
    void settingsModelEditsTheFallbackList()
    {
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setRefinementProvider(QStringLiteral("openai"));
        store->setRefinementFallbackProviders({QStringLiteral("anthropic"), QStringLiteral("local")});
        win::SettingsModel model(controller.get());
        QString target;
        for (const SettingsPaneGroup &group : model.schema().pane(QStringLiteral("refinement"))->groups) {
            for (const auto &row : model.section(group).rows) {
                if (row.id == QStringLiteral("refinementFallbacks")) {
                    target = row.targetPage;
                }
            }
        }
        QCOMPARE(target, QStringLiteral("refinement:fallbacks"));
        QCOMPARE(model.fallbackList(ProviderRole::Refinement).items.size(), 2);

        model.setValue(QStringLiteral("refinementFallbackList"),
                       withFallbackMoved(model.draft(), ProviderRole::Refinement, 0, 1));
        model.commit();
        QCOMPARE(store->refinementFallbackProviders(),
                 (QStringList{QStringLiteral("local"), QStringLiteral("anthropic")}));
    }

    // The Fallbacks row opens its subpage with the pane still selected, the
    // subpage's buttons and combo edit the stored list, and Back returns.
    void fallbacksSubpageNavigatesAndEdits()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setRefinementProvider(QStringLiteral("openai"));
        store->setRefinementFallbackProviders({QStringLiteral("anthropic"), QStringLiteral("local")});
        const FallbackListPresentation captions = fallbackListPresentation(
            ProviderRole::Refinement, store->snapshot(), {}, {}, FallbackSurface::Settings);
        const QString anthropic = QStringLiteral("anthropic");
        const QString local = QStringLiteral("local");
        const QString endpoint = QStringLiteral("endpoint");

        win::TranscribePane transcribe(controller.get());
        win::SettingsWindow window(controller.get(), &transcribe);
        window.showPage(QStringLiteral("refinement"));
        QTest::qWait(200);
        QVERIFY(window.pressForTest(
            win::SettingsModel(controller.get()).schema().row(QStringLiteral("refinementFallbacks"))->label));
        QTRY_COMPARE_WITH_TIMEOUT(window.shownPageForTest(), QStringLiteral("refinement:fallbacks"), 2000);
        QCOMPARE(window.selectedPaneForTest(), QStringLiteral("refinement"));
        QVERIFY(window.backVisibleForTest());

        // The first fallback can't move up; moving it down swaps the two.
        QVERIFY(!window.pressForTest(captions.moveUpCaption));
        QVERIFY(window.pressForTest(captions.moveDownCaption));
        QTRY_COMPARE_WITH_TIMEOUT(store->refinementFallbackProviders(), (QStringList{local, anthropic}), 2000);
        QTest::qWait(200);
        QVERIFY(window.pressForTest(captions.removeCaption));
        QTRY_COMPARE_WITH_TIMEOUT(store->refinementFallbackProviders(), QStringList{anthropic}, 2000);
        QTest::qWait(200);
        QVERIFY(window.chooseForTest(captions.addLabel,
                                     chainLabel(ProviderRole::Refinement, *controller->providerRegistry(),
                                                endpoint)));
        QTRY_COMPARE_WITH_TIMEOUT(store->refinementFallbackProviders(), (QStringList{anthropic, endpoint}), 2000);

        window.goBackForTest();
        QCOMPARE(window.shownPageForTest(), QStringLiteral("refinement"));
        QVERIFY(!window.backVisibleForTest());
        // A subpage's id opens it directly, as SPEECHER_GRAB_PAGE does.
        window.showPage(QStringLiteral("dictation:fallbacks"));
        QCOMPARE(window.shownPageForTest(), QStringLiteral("dictation:fallbacks"));
        QCOMPARE(window.selectedPaneForTest(), QStringLiteral("dictation"));
        window.close();
    }

    // The setup assistant's fallback section is optional: it never holds
    // Next, and it goes away with Skip cleanup.
    void setupFallbacksAreOptional()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        chooseRefinementProvider(*store, QStringLiteral("anthropic"));
        store->setRefinementFallbackProviders({});
        store->setLocalRunnerSettings({QStringLiteral("ollama"), QStringLiteral("qwen3:4b")});
        // An assistant of its own: the shared one carries earlier tests'
        // finished setup and taken shortcut.
        SetupWindow assistant(controller.get(), [] {});
        assistant.show(SetupAssistantPage::All);
        assistant.showPageForTest(QStringLiteral("refinement"));
        QVERIFY(assistant.fallbacksShownForTest());
        QVERIFY(assistant.finishEnabledForTest());

        // A running Local Runner with a cleanup model is suggested, and the
        // suggestion adds it after the other fallbacks. The step's own look
        // for runners finishes first.
        LocalSetup *local = controller->localSetup();
        const auto forgetRunners = qScopeGuard([local] { LocalSetupTestAccess::setRunners(*local, {}); });
        QTRY_VERIFY_WITH_TIMEOUT(!local->detectingRunners(), 20000);
        LocalSetupTestAccess::setRunners(*local, {{QStringLiteral("ollama"), QStringLiteral("Ollama"), {},
                                                   QStringLiteral("http://127.0.0.1:11434/v1"),
                                                   {QStringLiteral("qwen3:4b")}}});
        // QTRY evaluates its condition again once it holds, so the press is
        // latched: a second one would find the suggestion already gone.
        bool pressed = false;
        QTRY_VERIFY2_WITH_TIMEOUT(pressed || (pressed = assistant.pressFallbackSuggestionForTest()),
                                  qPrintable(QStringLiteral("primary %1, fallbacks %2")
                                                 .arg(store->refinementProvider(),
                                                      store->refinementFallbackProviders().join(QLatin1Char(',')))),
                                  2000);
        QTRY_COMPARE_WITH_TIMEOUT(store->refinementFallbackProviders(), QStringList{QStringLiteral("local")}, 2000);
        QVERIFY(assistant.finishEnabledForTest());

        store->setRefinementProvider(QStringLiteral("none"));
        assistant.showPageForTest(QStringLiteral("refinement"));
        QVERIFY(!assistant.fallbacksShownForTest());
        QVERIFY(assistant.finishEnabledForTest());
    }

    // A successful outcome shows a fix only when it has one; only then does
    // the panel take clicks, and the fix opens the page the error path opens.
    void outcomeOffersItsFixOnlyWithOne()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(31);
        controller->session()->popupMessageRequested(QStringLiteral("Pasted"), PopupOutcome::Inserted);
        QTest::qWait(100);
        QVERIFY(!panel->fixVisibleForTest());
        QVERIFY(panel->windowStyleForTest() & WS_EX_TRANSPARENT);

        panel->showForTest(32);
        controller->session()->popupMessageRequested(
            QStringLiteral("Pasted • Used Local Model. Your ChatGPT sign-in has expired."),
            PopupOutcome::Fallback, {ErrorFix::SettingsPage, QStringLiteral("accounts")});
        QTest::qWait(100);
        QVERIFY(panel->fixVisibleForTest());
        QVERIFY(!(panel->windowStyleForTest() & WS_EX_TRANSPARENT));
        QString opened;
        const auto connection = connect(panel, &DictationPanel::fixRequested, this,
                                        [&opened](const PopupErrorAction &fix) { opened = fix.pageId; });
        const auto release = qScopeGuard([connection] { QObject::disconnect(connection); });
        panel->pressFixForTest();
        QTRY_COMPARE_WITH_TIMEOUT(opened, QStringLiteral("accounts"), 2000);
        QVERIFY(!panel->visibleForTest());
        QTRY_VERIFY_WITH_TIMEOUT(FindWindowW(nullptr, L"Speecher") != nullptr, 2000);
    }

    // Pictures of every surface the fallbacks add, for UI evidence: both
    // settings pages with a chain, both subpages empty and full (with a
    // fallback that can't stand in, in the negative tone), both setup
    // steps, and an outcome with a fix, in Light and Dark.
    void fallbackEvidenceGrabs()
    {
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (grabDir.isEmpty()) {
            QSKIP("SPEECHER_TEST_GRAB_DIR is not set");
        }
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] {
            store->applySnapshot(original);
            LocalSetupTestAccess::setRunners(*controller->localSetup(), {});
            qunsetenv("SPEECHER_GRAB_PAGE");
            qunsetenv("SPEECHER_GRAB_SIZE");
            qunsetenv("SPEECHER_GRAB_SCROLL");
        });
        // An endpoint with no server can't stand in, which its row says in
        // the negative tone.
        store->setSpeechEndpointSettings({});
        store->setRefinementEndpointSettings({});
        qputenv("SPEECHER_GRAB_SIZE", "1000x1100");
        for (const QString theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            store->setTheme(theme);
            const auto grab = [&](const QString &page, const QString &name, const QByteArray &scroll = {}) {
                win::TranscribePane transcribe(controller.get());
                win::SettingsWindow window(controller.get(), &transcribe);
                window.show();
                qputenv("SPEECHER_GRAB_PAGE", page.toUtf8());
                qputenv("SPEECHER_GRAB_SCROLL", scroll);
                QTest::qWait(800);
                QVERIFY(window.capture(grabDir + QStringLiteral("/win-%1-%2.png").arg(name, theme)));
                window.close();
            };
            store->setSpeechProvider(QStringLiteral("codex"));
            store->setRefinementProvider(QStringLiteral("openai"));
            store->setSpeechFallbackProviders({});
            store->setRefinementFallbackProviders({});
            grab(QStringLiteral("dictation:fallbacks"), QStringLiteral("fallbacks-dictation-empty"));
            grab(QStringLiteral("refinement:fallbacks"), QStringLiteral("fallbacks-refinement-empty"));
            store->setSpeechFallbackProviders({QStringLiteral("claude"), QStringLiteral("endpoint")});
            store->setRefinementFallbackProviders({QStringLiteral("anthropic"), QStringLiteral("endpoint")});
            grab(QStringLiteral("dictation"), QStringLiteral("settings-dictation"));
            grab(QStringLiteral("refinement"), QStringLiteral("settings-refinement"));
            // The provider cards further down.
            grab(QStringLiteral("dictation"), QStringLiteral("settings-dictation-middle"), "middle");
            grab(QStringLiteral("refinement"), QStringLiteral("settings-refinement-middle"), "middle");
            grab(QStringLiteral("refinement"), QStringLiteral("settings-refinement-bottom"), "bottom");
            grab(QStringLiteral("dictation:fallbacks"), QStringLiteral("fallbacks-dictation-full"));
            grab(QStringLiteral("refinement:fallbacks"), QStringLiteral("fallbacks-refinement-full"));

            store->setSpeechFallbackProviders({});
            chooseRefinementProvider(*store, QStringLiteral("anthropic"));
            store->setRefinementFallbackProviders({QStringLiteral("openai")});
            store->setLocalRunnerSettings({QStringLiteral("ollama"), QStringLiteral("qwen3:4b")});
            SetupWindow assistant(controller.get(), [] {});
            assistant.show(SetupAssistantPage::All);
            for (const QString step : {QStringLiteral("transcription"), QStringLiteral("refinement")}) {
                assistant.showPageForTest(step);
                QTest::qWait(1500);
                QTRY_VERIFY_WITH_TIMEOUT(!controller->localSetup()->detectingRunners(), 20000);
                // As a running Ollama would leave it, so Refinement suggests it.
                LocalSetupTestAccess::setRunners(*controller->localSetup(),
                                                 {{QStringLiteral("ollama"), QStringLiteral("Ollama"), {},
                                                   QStringLiteral("http://127.0.0.1:11434/v1"),
                                                   {QStringLiteral("qwen3:4b")}}});
                QTest::qWait(300);
                assistant.revealFallbacksForTest();
                QTest::qWait(300);
                QVERIFY(assistant.captureForTest(grabDir + QStringLiteral("/win-setup-%1-fallbacks-%2.png")
                                                               .arg(step, theme)));
            }
        }

        DictationPanel *panel = frontEnd->dictationPanelForTest();
        panel->showForTest(41);
        controller->session()->popupMessageRequested(
            QStringLiteral("Pasted • Used Local Model. Your ChatGPT sign-in has expired."),
            PopupOutcome::Fallback, {ErrorFix::SettingsPage, QStringLiteral("accounts")});
        QTest::qWait(300);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-fix.png")));
        panel->showForTest(42);
        controller->session()->popupMessageRequested(
            QStringLiteral("Pasted • Transcribed with Custom Endpoint. ChatGPT Codex couldn't be reached."),
            PopupOutcome::Fallback);
        QTest::qWait(300);
        QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-receipt-fallback.png")));
        panel->dismissForTest();
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
