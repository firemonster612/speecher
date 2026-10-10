#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "core/settings/FallbackPresentation.h"
#include "core/settings/ProviderRatings.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupGeometry.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
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
#include <QHash>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <optional>
#include <utility>

namespace speecher {
namespace {

bool nativeUiAvailable()
{
    return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

// A press and release of one key, as the person's keyboard would send it to
// whichever window has focus.
void pressKey(WORD key)
{
    INPUT input[2]{};
    for (INPUT &event : input) {
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = key;
        event.ki.wScan = WORD(MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    }
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
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
    for (const RowOption &provider : registry.rowOptions(role)) {
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

// A pane's rows as the model shows them now, by id.
QHash<QString, win::RowSnapshot> rowsOnPane(const win::SettingsModel &model, const QString &pane)
{
    QHash<QString, win::RowSnapshot> rows;
    for (const SettingsPaneGroup &group : model.schema().pane(pane)->groups) {
        for (const win::RowSnapshot &row : model.section(group).rows) {
            rows.insert(row.id, row);
        }
    }
    return rows;
}

// A one-word selection edit, and a long one whose two changes are far apart,
// so its review folds the words between them away.
SelectionEditReview shortReview(const ReviewKeys &keys)
{
    return selectionEditReview(QStringLiteral("Move the standup to Wednesday."),
                               QStringLiteral("Move the standup to Thursday."),
                               {QStringLiteral("make it Thursday")}, keys);
}

SelectionEditReview longReview(const ReviewKeys &keys)
{
    const QString middle =
        QStringLiteral("The quarterly numbers look steady and nobody expects surprises this time. ").repeated(18);
    return selectionEditReview(QStringLiteral("Their report says ") + middle + QStringLiteral("See you their."),
                               QStringLiteral("The report says ") + middle + QStringLiteral("See you there."),
                               {QStringLiteral("fix the grammar")}, keys);
}

// An edit that keeps too few words to compare, shown whole as a rewrite.
SelectionEditReview rewriteReview(const ReviewKeys &keys)
{
    return selectionEditReview(
        QStringLiteral("hey can someone look at the build its broken again since marco merged"),
        QStringLiteral("Good morning. The build is failing again, most likely because of the change "
                       "Marco merged last night. Could someone investigate before the stand-up?"),
        {QStringLiteral("make this more formal")}, keys);
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

    void customCheckIntervalSnapshotCarriesItsUnit()
    {
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        win::SettingsModel model(controller.get());
        model.setValue(QStringLiteral("updateCheckInterval"), QStringLiteral("custom"));
        model.setValue(QStringLiteral("updateCheckCustomInterval"),
                       QVariantMap{{QStringLiteral("number"), 8}, {QStringLiteral("unit"), QStringLiteral("hours")}});
        model.commit();

        std::optional<win::RowSnapshot> custom;
        for (const SettingsPaneGroup &group : model.schema().pane(QStringLiteral("general"))->groups) {
            for (const auto &row : model.section(group).rows) {
                if (row.id == QStringLiteral("updateCheckCustomInterval")) {
                    custom = row;
                }
            }
        }
        QVERIFY(custom);
        QCOMPARE(custom->units.size(), 3);
        QCOMPARE(custom->value.toMap(),
                 (QVariantMap{{QStringLiteral("number"), 8}, {QStringLiteral("unit"), QStringLiteral("hours")}}));
        QCOMPARE(store->updateCheckIntervalMinutes(), 480);
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

    // A selection edit's review takes the capsule's place, as wide as an
    // error's wrapped text plus the preview's margins, takes clicks without
    // taking focus, and leaves when the session ends the review, not on a
    // state.
    void nativeSelectionEditReviewTakesTheCapsulesPlace()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        DictationSession *session = controller->session();
        panel->showForTest(41);
        session->stateChanged(dictationStateName(DictationState::Reviewing));
        session->popupSelectionEditReviewRequested(shortReview({escapeKeyName(), enterKeyName()}));
        // WinUI lays the card out in the island's new size a moment after
        // the window takes it.
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describeBoxes({panel->reviewGeometryForTest()})));
        const QRect card = panel->reviewGeometryForTest();
        QVERIFY2(std::abs(card.width() - (520 + 2 * 16) * dipScale()) <= 1, qPrintable(describeBoxes({card})));
        QVERIFY2(panel->capsuleGeometryForTest().isEmpty(),
                 qPrintable(describeBoxes({card, panel->capsuleGeometryForTest()})));
        const qintptr style = panel->windowStyleForTest();
        QVERIFY(style & WS_EX_NOACTIVATE);
        QVERIFY(!(style & WS_EX_TRANSPARENT));
        QVERIFY(panel->reviewReplaceIsDefaultForTest());
        // A short edit shows whole, with nothing to unfold or scroll.
        QVERIFY(panel->reviewToggleForTest().isEmpty());
        QVERIFY(!panel->reviewScrollsForTest());
        QCOMPARE(panel->reviewTextForTest(),
                 QStringLiteral("Move the standup to Wednesday Thursday."));

        // A taller review in its place still shows whole, and without Enter,
        // Replace is an ordinary button.
        session->popupSelectionEditReviewRequested(rewriteReview({escapeKeyName(), QString()}));
        QVERIFY(!panel->reviewReplaceIsDefaultForTest());
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describeBoxes({panel->reviewGeometryForTest()})));
        QVERIFY(panel->reviewGeometryForTest().height() > card.height());
        QVERIFY(std::abs(panel->reviewGeometryForTest().bottom() - card.bottom()) <= 1);

        session->stateChanged(dictationStateName(DictationState::Delivering));
        QVERIFY(!panel->reviewGeometryForTest().isEmpty());
        session->popupSelectionEditReviewEnded();
        QVERIFY(panel->reviewGeometryForTest().isEmpty());
        QVERIFY(!panel->capsuleGeometryForTest().isEmpty());
        session->stateChanged(dictationStateName(DictationState::Idle));
        panel->dismissForTest();
    }

    void nativeSelectionEditReviewButtonsKeepOrReplace()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        DictationSession *session = controller->session();
        QSignalSpy kept(panel, &DictationPanel::keepOriginalRequested);
        QSignalSpy replaced(panel, &DictationPanel::replaceSelectionRequested);
        panel->showForTest(42);
        session->stateChanged(dictationStateName(DictationState::Reviewing));
        session->popupSelectionEditReviewRequested(shortReview({escapeKeyName(), enterKeyName()}));
        QTRY_VERIFY(panel->reviewFullyVisibleForTest());
        panel->pressKeepOriginalForTest();
        QTRY_COMPARE(kept.count(), 1);
        QCOMPARE(replaced.count(), 0);
        panel->pressReplaceForTest();
        QTRY_COMPARE(replaced.count(), 1);
        QCOMPARE(kept.count(), 1);
        session->popupSelectionEditReviewEnded();
        session->stateChanged(dictationStateName(DictationState::Idle));
        panel->dismissForTest();
    }

    // A long edit shows its changes with the words between left out, Show
    // all brings them back, and past its line limit the edit scrolls.
    void nativeLongSelectionEditFoldsAndScrolls()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        DictationSession *session = controller->session();
        const SelectionEditReview review = longReview({escapeKeyName(), enterKeyName()});
        QVERIFY(!review.folded.isEmpty());
        panel->showForTest(43);
        session->stateChanged(dictationStateName(DictationState::Reviewing));
        session->popupSelectionEditReviewRequested(review);
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describeBoxes({panel->reviewGeometryForTest()})));
        const QChar ellipsis(0x2026);
        QVERIFY(panel->reviewTextForTest().contains(ellipsis));
        QCOMPARE(panel->reviewToggleForTest(), QStringLiteral("Show all"));
        const QRect folded = panel->reviewGeometryForTest();

        panel->pressReviewToggleForTest();
        QTRY_VERIFY(!panel->reviewTextForTest().contains(ellipsis));
        QCOMPARE(panel->reviewToggleForTest(), QStringLiteral("Show less"));
        QTRY_VERIFY(panel->reviewScrollsForTest());
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describeBoxes({panel->reviewGeometryForTest()})));
        // The card grows up from where the capsule sits, once WinUI has laid
        // it out at the island's new size.
        const auto keptBottom = [&](bool taller) {
            const QRect now = panel->reviewGeometryForTest();
            return std::abs(now.bottom() - folded.bottom()) <= 1
                && (taller ? now.height() > folded.height() : std::abs(now.height() - folded.height()) <= 1);
        };
        QTRY_VERIFY2(keptBottom(true), qPrintable(describeBoxes({folded, panel->reviewGeometryForTest()})));

        panel->pressReviewToggleForTest();
        QTRY_VERIFY(panel->reviewTextForTest().contains(ellipsis));
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describeBoxes({panel->reviewGeometryForTest()})));
        QTRY_VERIFY2(keptBottom(false), qPrintable(describeBoxes({folded, panel->reviewGeometryForTest()})));
        session->popupSelectionEditReviewEnded();
        session->stateChanged(dictationStateName(DictationState::Idle));
        panel->dismissForTest();
    }

    // While a follow-up is dictated the card stays up, dimmed and without its
    // footer, above the ordinary capsule, through listening and refining,
    // and keeps the Show all it had. Without the follow-up it is alone again.
    void nativeFollowUpShowsTheCardOverTheCapsule()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        DictationPanel *panel = frontEnd->dictationPanelForTest();
        DictationSession *session = controller->session();
        const QChar ellipsis(0x2026);
        const auto describe = [panel] {
            return describeBoxes({panel->reviewGeometryForTest(), panel->capsuleGeometryForTest()});
        };
        SelectionEditReview review = longReview({escapeKeyName(), enterKeyName(), QStringLiteral("Ctrl+Space")});
        panel->showForTest(45);
        session->stateChanged(dictationStateName(DictationState::Reviewing));
        session->popupSelectionEditReviewRequested(review);
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest(), qPrintable(describe()));
        QCOMPARE(panel->reviewHintForTest(), QStringLiteral("Press Ctrl+Space to ask for more changes"));
        QVERIFY(!panel->reviewFollowingForTest());
        panel->pressReviewToggleForTest();
        QTRY_VERIFY(!panel->reviewTextForTest().contains(ellipsis));

        // The session shows the review again, following, then starts a
        // dictation of its own.
        review.following = true;
        session->popupSelectionEditReviewRequested(review);
        // As a session start does: a preview frozen before would drop the
        // follow-up's words.
        session->popupFrozenChanged(false);
        panel->showForTest(46);
        session->stateChanged(dictationStateName(DictationState::Starting));
        session->stateChanged(dictationStateName(DictationState::Listening));
        panel->drivePreviewForTest(QStringLiteral("actually make it Friday"));
        const auto cardOverCapsule = [panel] {
            const QRect card = panel->reviewGeometryForTest();
            const QRect capsule = panel->capsuleGeometryForTest();
            return panel->reviewFullyVisibleForTest() && !capsule.isEmpty() && capsule.top() > card.bottom()
                && std::abs(capsule.center().x() - card.center().x()) <= 1;
        };
        QTRY_VERIFY2(cardOverCapsule(), qPrintable(describe()));
        QVERIFY(panel->reviewFollowingForTest());
        QVERIFY(!panel->reviewTextForTest().contains(ellipsis));
        QVERIFY(!panel->previewGeometryForTest().isEmpty());
        QVERIFY(!panel->pauseGeometryForTest().isEmpty());
        QVERIFY(!(panel->windowStyleForTest() & WS_EX_TRANSPARENT));

        session->stateChanged(dictationStateName(DictationState::Stopping));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        session->stateChanged(dictationStateName(DictationState::Refining));
        session->popupRefiningChanged(true);
        QTRY_VERIFY2(cardOverCapsule() && !panel->spinnerGeometryForTest().isEmpty(), qPrintable(describe()));

        // The edit comes back for review: the card alone, still whole.
        session->popupRefiningChanged(false);
        review.following = false;
        session->stateChanged(dictationStateName(DictationState::Reviewing));
        session->popupSelectionEditReviewRequested(review);
        QTRY_VERIFY2(panel->reviewFullyVisibleForTest() && panel->capsuleGeometryForTest().isEmpty(),
                     qPrintable(describe()));
        QVERIFY(!panel->reviewFollowingForTest());
        QVERIFY(!panel->reviewTextForTest().contains(ellipsis));
        session->popupSelectionEditReviewEnded();
        QVERIFY(panel->reviewGeometryForTest().isEmpty());
        session->stateChanged(dictationStateName(DictationState::Idle));
        panel->dismissForTest();
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

        // A selection edit's review in the capsule's place: a short edit, a
        // long one folded, whole and folded again, and a rewrite while another
        // app holds Enter, so Replace is an ordinary button. Each picture
        // waits for WinUI to lay the card out in the window's new size, then
        // for a frame to reach the screen.
        const auto grabReview = [&](const QString &name) {
            QTRY_VERIFY2(panel->reviewFullyVisibleForTest(),
                         qPrintable(name + describeBoxes({panel->reviewGeometryForTest()})));
            QTest::qWait(100);
            QVERIFY(panel->saveGrabForTest(grabDir + QStringLiteral("/win-review%1.png").arg(name)));
        };
        const ReviewKeys keys{escapeKeyName(), enterKeyName()};
        panel->showForTest(44);
        controller->session()->stateChanged(dictationStateName(DictationState::Reviewing));
        controller->session()->popupSelectionEditReviewRequested(selectionEditReview(
            QStringLiteral("Can we move the standup to Wednesday? Half the team is out on Thursday "
                           "and I'd rather not run it with three people."),
            QStringLiteral("Can we move the standup to Thursday? Half the team is out on Wednesday "
                           "and I'd rather not run it with three people."),
            {QStringLiteral("swap the two days")}, keys));
        grabReview(QString());
        controller->session()->popupSelectionEditReviewRequested(longReview(keys));
        grabReview(QStringLiteral("-folded"));
        panel->pressReviewToggleForTest();
        grabReview(QStringLiteral("-whole"));
        panel->pressReviewToggleForTest();
        grabReview(QStringLiteral("-less"));
        controller->session()->popupSelectionEditReviewRequested(rewriteReview({escapeKeyName(), QString()}));
        grabReview(QStringLiteral("-rewritten"));
        controller->session()->popupSelectionEditReviewEnded();
        // The hint on asking for more changes, then a follow-up dictated
        // under the dimmed card: listening with live words, then refining.
        SelectionEditReview following = selectionEditReview(
            QStringLiteral("Can we move the standup to Wednesday? Half the team is out on Thursday "
                           "and I'd rather not run it with three people."),
            QStringLiteral("Can we move the standup to Thursday? Half the team is out on Wednesday "
                           "and I'd rather not run it with three people."),
            {QStringLiteral("swap the two days")}, {escapeKeyName(), enterKeyName(), QStringLiteral("Ctrl+Space")});
        controller->session()->popupSelectionEditReviewRequested(following);
        grabReview(QStringLiteral("-hint"));
        following.following = true;
        controller->session()->popupSelectionEditReviewRequested(following);
        // As a session start does; the frozen-preview grab above left the
        // preview frozen, which drops live words.
        controller->session()->popupFrozenChanged(false);
        panel->showForTest(45);
        controller->session()->stateChanged(dictationStateName(DictationState::Listening));
        panel->drivePreviewForTest(QStringLiteral("actually make it Friday"));
        for (int i = 0; i < 20; ++i) {
            panel->driveLevelForTest(i % 2 ? 0.2f : 0.7f);
            QTest::qWait(24);
        }
        grabReview(QStringLiteral("-following"));
        controller->session()->stateChanged(dictationStateName(DictationState::Refining));
        panel->driveStatusForTest(QStringLiteral("Stopping"));
        controller->session()->popupRefiningChanged(true);
        grabReview(QStringLiteral("-following-refining"));
        controller->session()->popupRefiningChanged(false);
        controller->session()->popupSelectionEditReviewEnded();
        controller->session()->stateChanged(dictationStateName(DictationState::Idle));
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

    // Setup's Global Shortcut step asks for the optional Cancel and Pause
    // Shortcuts under the dictation one, as the Dictation pane lists them, and
    // their recorder takes Escape as the key instead of closing.
    void sessionShortcutRowsTakeABareKey()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        const auto clear = qScopeGuard([this] {
            for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
                controller->setGlobalShortcut({}, nullptr, role);
            }
        });
        QVERIFY(controller->setGlobalShortcut(QKeySequence(Qt::Key_P), nullptr, GlobalShortcutRole::Pause));
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");

        setup->show(SetupAssistantPage::All);
        setup->showPageForTest(QStringLiteral("shortcut"));
        setup->recordShortcutForTest(GlobalShortcutRole::Cancel);
        QTest::qWait(500);
        pressKey(VK_ESCAPE);
        QTest::qWait(300);
        if (!grabDir.isEmpty()) {
            QVERIFY(setup->captureForTest(grabDir + QStringLiteral("/win-setup-record-escape.png")));
        }
        // Enter saves: a bare Enter is the dialog's default button.
        pressKey(VK_RETURN);
        QTRY_COMPARE_WITH_TIMEOUT(controller->globalShortcut(GlobalShortcutRole::Cancel),
                                  ShortcutBinding(QKeySequence(Qt::Key_Escape)), 2000);
        if (!grabDir.isEmpty()) {
            QTest::qWait(400);
            setup->scrollToEndForTest();
            QTest::qWait(400);
            QVERIFY(setup->captureForTest(grabDir + QStringLiteral("/win-setup-session-shortcuts.png")));
        }

        win::TranscribePane transcribe(controller.get());
        win::SettingsWindow window(controller.get(), &transcribe);
        window.show();
        if (!grabDir.isEmpty()) {
            qputenv("SPEECHER_GRAB_PAGE", "dictation");
            const auto unset = qScopeGuard([] { qunsetenv("SPEECHER_GRAB_PAGE"); });
            QVERIFY(window.capture(grabDir + QStringLiteral("/win-settings-shortcuts.png")));
        }
        window.close();
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

    // A primary that can't work right now says so in the negative tone.
    void settingsModelCarriesThePrimaryStatusTone()
    {
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        chooseRefinementProvider(*store, QStringLiteral("endpoint"));
        store->setRefinementEndpointSettings({});
        win::SettingsModel model(controller.get());
        std::optional<win::RowSnapshot> picker;
        for (const SettingsPaneGroup &group : model.schema().pane(QStringLiteral("refinement"))->groups) {
            for (const auto &row : model.section(group).rows) {
                if (row.id == QStringLiteral("refinementProvider")) {
                    picker = row;
                }
            }
        }
        QVERIFY(picker);
        QVERIFY2(picker->help.startsWith(QStringLiteral("No server URL is set.")), qPrintable(picker->help));
        QCOMPARE(picker->helpTone, StatusTone::Negative);
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
        QTest::qWait(200);
        // Both Removes before the list is drawn again: the second still finds
        // its fallback, now first.
        QVERIFY(window.pressForTest(captions.removeCaption, 0));
        QVERIFY(window.pressForTest(captions.removeCaption, 1));
        QTRY_COMPARE_WITH_TIMEOUT(store->refinementFallbackProviders(), QStringList{}, 2000);

        window.goBackForTest();
        QCOMPARE(window.shownPageForTest(), QStringLiteral("refinement"));
        QVERIFY(!window.backVisibleForTest());
        // A subpage's id opens it directly, as SPEECHER_GRAB_PAGE does.
        window.showPage(QStringLiteral("dictation:fallbacks"));
        QCOMPARE(window.shownPageForTest(), QStringLiteral("dictation:fallbacks"));
        QCOMPARE(window.selectedPaneForTest(), QStringLiteral("dictation"));
        // Closed on a subpage, the window opens again on Home.
        window.close();
        window.show();
        QCOMPARE(window.shownPageForTest(), QStringLiteral("home"));
        QCOMPARE(window.selectedPaneForTest(), QStringLiteral("home"));
        QVERIFY(!window.backVisibleForTest());
        window.close();
    }

    // The copyToPhone action opens the dialog, and the transfer listens until
    // it closes. With SPEECHER_TEST_GRAB_DIR set, also saves the open dialog.
    void phoneTransferListensWhileItsDialogIsOpen()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        win::SettingsWindow *window = frontEnd->settingsWindowForTest();
        const auto close = qScopeGuard([window] { window->close(); });
        window->showPage(QStringLiteral("general"));
        QTest::qWait(200);
        frontEnd->actionTriggered(QStringLiteral("copyToPhone"));
        QVERIFY(window->phoneTransferOpenForTest());
        if (const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR"); !grabDir.isEmpty()) {
            QTest::qWait(500);
            QVERIFY(window->capture(grabDir + QStringLiteral("/phone-transfer.png")));
        }
        QVERIFY(window->closeDialogForTest());
        QTRY_VERIFY_WITH_TIMEOUT(!window->phoneTransferOpenForTest(), 2000);
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
        // An outcome, not an error: no Dismiss, no countdown, and the session
        // stays where it was.
        QVERIFY(!panel->errorChromeVisibleForTest());
        const DictationState state = controller->session()->state();
        win::SettingsWindow *settings = frontEnd->settingsWindowForTest();
        settings->close();
        QVERIFY(!settings->isVisible());
        panel->pressFixForTest();
        QTRY_VERIFY_WITH_TIMEOUT(settings->isVisible(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(settings->shownPageForTest(), QStringLiteral("accounts"), 2000);
        QVERIFY(!panel->visibleForTest());
        QVERIFY(!panel->errorChromeVisibleForTest());
        QCOMPARE(controller->session()->state(), state);
        settings->close();
    }

    // Pictures of every surface the fallbacks add, for UI evidence: both
    // settings pages with a chain, Dictation with a primary that can't
    // work, both subpages empty and full (with a fallback that can't stand
    // in, in the negative tone), both setup steps, and an outcome with a
    // fix, in Light and Dark.
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
            // Profiles with services of their own: Email refines with a Claude
            // model, Personal dictates with ChatGPT Codex.
            AppSettings profiles = store->snapshot();
            profiles.refinement.writingProfiles[1].refinementProvider = QStringLiteral("anthropic");
            profiles.refinement.writingProfiles[1].refinementModel = QStringLiteral("claude-sonnet-5-5");
            profiles.refinement.writingProfiles[2].speechProvider = QStringLiteral("codex");
            store->applySnapshot(profiles);
            grab(QStringLiteral("writingProfiles"), QStringLiteral("settings-profiles"));
            grab(QStringLiteral("writingProfiles"), QStringLiteral("settings-profiles-middle"), "middle");
            // A primary that can't work: its picker says so in the negative
            // tone.
            store->setSpeechProvider(QStringLiteral("endpoint"));
            grab(QStringLiteral("dictation"), QStringLiteral("settings-dictation-primary-status"));
            store->setSpeechProvider(QStringLiteral("codex"));

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
            // Custom Endpoint chosen: its fields under the choices, waiting
            // for a server, which holds the step until one is saved.
            store->setSpeechProvider(QStringLiteral("endpoint"));
            assistant.showPageForTest(QStringLiteral("transcription"));
            QTest::qWait(1500);
            QVERIFY(!assistant.finishEnabledForTest());
            QVERIFY(assistant.captureForTest(
                grabDir + QStringLiteral("/win-setup-transcription-endpoint-%1.png").arg(theme)));
            SpeechEndpointSettings server;
            server.baseUrl = QStringLiteral("http://127.0.0.1:8080/v1");
            store->setSpeechEndpointSettings(server);
            assistant.showPageForTest(QStringLiteral("transcription"));
            QTRY_VERIFY_WITH_TIMEOUT(assistant.finishEnabledForTest(), 5000);
            store->setSpeechEndpointSettings({});
            store->setSpeechProvider(QStringLiteral("codex"));
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

    // The Rating and Advanced rows carry what Windows draws: the chosen
    // service's bars and models, and neither row for Custom Endpoint.
    void settingsModelCarriesProviderRatings()
    {
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setSpeechProvider(QStringLiteral("codex"));
        chooseRefinementProvider(*store, QStringLiteral("openai"));
        {
            const win::SettingsModel model(controller.get());
            const QHash<QString, win::RowSnapshot> dictation = rowsOnPane(model, QStringLiteral("dictation"));
            const win::RowSnapshot rating = dictation.value(QStringLiteral("speechRating"));
            QCOMPARE(rating.kind, RowKind::Rating);
            QCOMPARE(rating.ratings,
                     (QList<Rating>{{RatingMeasure::Accuracy, 8.5}, {RatingMeasure::Speed, 7.0}}));
            const win::RowSnapshot models = dictation.value(QStringLiteral("speechModels"));
            QCOMPARE(models.kind, RowKind::ModelList);
            QCOMPARE(models.ratedModels.size(), 2);
            QCOMPARE(models.ratedModels.first().name, QStringLiteral("GPT Live Transcribe"));
            const QHash<QString, win::RowSnapshot> refinement = rowsOnPane(model, QStringLiteral("refinement"));
            QCOMPARE(refinement.value(QStringLiteral("refinementRating")).ratings,
                     (QList<Rating>{{RatingMeasure::Quality, 10.0}, {RatingMeasure::Speed, 7.0}}));
        }
        store->setSpeechProvider(QStringLiteral("endpoint"));
        chooseRefinementProvider(*store, QStringLiteral("endpoint"));
        const win::SettingsModel model(controller.get());
        const QHash<QString, win::RowSnapshot> dictation = rowsOnPane(model, QStringLiteral("dictation"));
        QVERIFY(!dictation.contains(QStringLiteral("speechRating")));
        QVERIFY(!dictation.contains(QStringLiteral("speechModels")));
        QVERIFY(!rowsOnPane(model, QStringLiteral("refinement")).contains(QStringLiteral("refinementRating")));
    }

    // The Rating row draws the chosen service's bars and Advanced stays open
    // across the rebuild a new choice causes; Custom Endpoint shows neither.
    void settingsDrawsRatingsAndKeepsAdvancedOpen()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setSpeechProvider(QStringLiteral("codex"));
        const QString service =
            win::SettingsModel(controller.get()).schema().row(QStringLiteral("speechProvider"))->label;
        const QString advanced = providerModelsCaption();
        const ProviderRegistry &registry = *controller->providerRegistry();

        win::TranscribePane transcribe(controller.get());
        win::SettingsWindow window(controller.get(), &transcribe);
        window.showPage(QStringLiteral("dictation"));
        QTest::qWait(200);
        QCOMPARE(window.ratingBarsForTest().join(QStringLiteral(", ")), QStringLiteral("Accuracy 8.5, Speed 7"));
        QVERIFY(!window.expandedForTest(advanced));
        QVERIFY(window.expandForTest(advanced));

        QVERIFY(window.chooseForTest(service, chainLabel(ProviderRole::Speech, registry, QStringLiteral("claude"))));
        QTRY_COMPARE_WITH_TIMEOUT(window.ratingBarsForTest().join(QStringLiteral(", ")),
                                  QStringLiteral("Accuracy 5.5, Speed 10"), 2000);
        QVERIFY(window.expandedForTest(advanced));

        QVERIFY(window.chooseForTest(service, chainLabel(ProviderRole::Speech, registry, QStringLiteral("endpoint"))));
        QTRY_VERIFY_WITH_TIMEOUT(window.ratingBarsForTest().isEmpty(), 2000);
        QVERIFY(!window.expandForTest(advanced));
        window.close();
    }

    // Each rated option in the setup steps shows its bars and Custom Endpoint
    // none; the chosen service lists its models under Advanced.
    void setupOptionsShowTheirRatings()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        SettingsStore *store = controller->settings();
        const AppSettings original = store->snapshot();
        const auto restore = qScopeGuard([&] { store->applySnapshot(original); });
        store->setSpeechProvider(QStringLiteral("codex"));
        chooseRefinementProvider(*store, QStringLiteral("openai"));
        SetupWindow assistant(controller.get(), [] {});
        assistant.show(SetupAssistantPage::All);

        assistant.showPageForTest(QStringLiteral("transcription"));
        const QStringList speech = assistant.ratedOptionsForTest();
        QVERIFY2(speech.contains(QStringLiteral("codex")) && speech.contains(QStringLiteral("claude")),
                 qPrintable(speech.join(QLatin1Char(','))));
        QVERIFY(assistant.modelsShownForTest());

        assistant.showPageForTest(QStringLiteral("refinement"));
        const QStringList refinement = assistant.ratedOptionsForTest();
        QVERIFY2(refinement.contains(QStringLiteral("openai")) && refinement.contains(QStringLiteral("anthropic"))
                     && !refinement.contains(QStringLiteral("endpoint")),
                 qPrintable(refinement.join(QLatin1Char(','))));
        QVERIFY(assistant.modelsShownForTest());

        chooseRefinementProvider(*store, QStringLiteral("endpoint"));
        assistant.showPageForTest(QStringLiteral("refinement"));
        QVERIFY(!assistant.modelsShownForTest());
    }

    // Pictures of the ratings for UI evidence, in Light: the setup
    // Transcription step with ChatGPT Codex and with Local Model chosen and
    // the Refinement step with OpenAI, each as it opens and with Advanced
    // open, and the Settings Dictation page (Advanced open) and Refinement
    // page. The screen is shorter than the pages, so each view scrolls to
    // what it shows.
    void ratingEvidenceGrabs()
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
            qunsetenv("SPEECHER_GRAB_PAGE");
            qunsetenv("SPEECHER_GRAB_SIZE");
            qunsetenv("SPEECHER_GRAB_SCROLL");
        });
        store->setTheme(QStringLiteral("light"));
        store->setSpeechFallbackProviders({});
        store->setRefinementFallbackProviders({});
        store->setSpeechProvider(QStringLiteral("codex"));
        chooseRefinementProvider(*store, QStringLiteral("openai"));

        qputenv("SPEECHER_GRAB_SIZE", "1000x800");
        // Dictation from its Transcription card down.
        for (const auto &[page, scroll] : {std::pair{QStringLiteral("dictation"), QByteArray("400")},
                                           std::pair{QStringLiteral("refinement"), QByteArray()}}) {
            win::TranscribePane transcribe(controller.get());
            win::SettingsWindow window(controller.get(), &transcribe);
            window.show();
            window.showPage(page);
            QTest::qWait(800);
            window.expandForTest(providerModelsCaption());
            qputenv("SPEECHER_GRAB_PAGE", page.toUtf8());
            qputenv("SPEECHER_GRAB_SCROLL", scroll);
            QVERIFY(window.capture(grabDir + QStringLiteral("/settings-%1.png").arg(page)));
            window.close();
        }

        qputenv("SPEECHER_GRAB_SIZE", "760x800");
        const auto grabStep = [&](const QString &step, const QString &name) {
            SetupWindow assistant(controller.get(), [] {});
            assistant.keepSavedProvidersForTest();
            assistant.show(SetupAssistantPage::All);
            assistant.showPageForTest(step);
            QTest::qWait(1500);
            QVERIFY(assistant.captureForTest(grabDir + QStringLiteral("/setup-%1.png").arg(name)));
            assistant.revealModelsForTest();
            QTest::qWait(500);
            QVERIFY(assistant.captureForTest(grabDir + QStringLiteral("/setup-%1-advanced.png").arg(name)));
            assistant.scrollToEndForTest();
            QTest::qWait(500);
            QVERIFY(assistant.captureForTest(grabDir + QStringLiteral("/setup-%1-end.png").arg(name)));
        };
        grabStep(QStringLiteral("transcription"), QStringLiteral("transcription-codex"));
        grabStep(QStringLiteral("refinement"), QStringLiteral("refinement-openai"));
        // Local Model is only a choice where this build runs speech models.
        if (!controller->providerRegistry()->speechProvider(QStringLiteral("local"))) {
            return;
        }
        store->setSpeechProvider(QStringLiteral("local"));
        controller->localSetup()->probeHardware();
        QTRY_VERIFY_WITH_TIMEOUT(controller->localSetup()->hardwareKnown(), 60000);
        grabStep(QStringLiteral("transcription"), QStringLiteral("transcription-local"));
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
