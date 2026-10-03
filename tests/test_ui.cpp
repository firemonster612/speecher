#include <QTcpSocket>
#include <QToolButton>
#include <QTableWidget>
#include "common/test_prelude.h"
#include "common/test_doubles.h"
#include "common/test_auth.h"
#include "common/test_local_setup.h"
#include "app/SetupSteps.h"
#include "core/VocabularyLimit.h"
#include "app/AccessibilityPresentation.h"
#include "ui/AccessibilityNotice.h"
#include "ui/InlineMessage.h"
#include "ui/InsightsCharts.h"
#include "core/SecretStore.h"
#include "app/LocalSetup.h"
#include "app/UpdateBanner.h"
#include "dictation/PopupPresentation.h"
#include "providers/LocalModelStore.h"
#include "providers/EndpointSpeechTranscriber.h"
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QTcpServer>
#include "app/MicrophoneTest.h"
#include "frontend/qt/MicrophoneTestRow.h"
#include "frontend/qt/OutputCustomRows.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetupFlow.h"
#endif
#include "frontend/qt/ProviderCustomRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/settings/SettingsPageSet.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>

#include <algorithm>
#include <memory>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QRegularExpression>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QListWidget>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleHints>
#include <QTableWidget>
#include <QTextBoundaryFinder>
#include <QVBoxLayout>

using namespace speecher::test;

namespace {

// The migrated pages are the generic renderer over the core schema, so a test
// builds them the same way the front end does.
std::unique_ptr<SchemaSettingsPage> schemaPage(const QString &id,
                                               const PlatformComposition &platform,
                                               const ProviderRegistry &providers,
                                               SchemaCustomRowFactory customRows = {})
{
    const SettingsSchema schema =
        buildSettingsSchema(qtSchemaContext(platform, providers));
    return std::make_unique<SchemaSettingsPage>(schema.page(id).sections, nullptr, std::move(customRows));
}

// The record dialog a collection editor has open. A closed one lingers until
// the event loop deletes it, so only a shown one counts.
QDialog *shownRecordDialog(const QWidget &page)
{
    for (QDialog *dialog : page.findChildren<QDialog *>(QStringLiteral("collectionRecordDialog"))) {
        if (dialog->isVisible()) {
            return dialog;
        }
    }
    return nullptr;
}

void acceptRecordDialog(QDialog *dialog)
{
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}

QStringList sectionLabels(const QWidget &page, const QWidget *except = nullptr)
{
    // Section titles are the headers above each card, in top-to-bottom order;
    // headings inside except (a custom block's own) are not the page's.
    QList<QPair<int, QString>> titles;
    for (QLabel *label : page.findChildren<QLabel *>(QStringLiteral("sectionLabel"))) {
        if (except && except->isAncestorOf(label)) {
            continue;
        }
        titles.append({label->mapTo(&page, QPoint()).y(), label->text()});
    }
    std::sort(titles.begin(), titles.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList labels;
    for (const auto &title : titles) {
        labels.append(title.second);
    }
    return labels;
}

class SizingPopupPositioner final : public PopupPositioner {
public:
    void configurePopup(PopupSurface &surface) override
    {
        configuredSize = surface.preferredSize();
    }

    void positionBottomCenter(PopupSurface &) override
    {
    }

    QSize configuredSize;
};

#ifdef Q_OS_LINUX
// Points the audio seam, which only LinuxComposition reads, at two seconds of
// a loud square wave, so the real row can be driven without a sound server.
// Unset on scope exit.
auto feedMicrophoneFromTone(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("tone.wav"));
    QByteArray samples;
    for (int i = 0; i < 32000; ++i) {
        const qint16 sample = (i / 20) % 2 ? 16000 : -16000;
        samples.append(reinterpret_cast<const char *>(&sample), sizeof sample);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(test::wavBytes(samples, 16000, 1)) < 0) {
        qFatal("cannot write %s", qPrintable(path));
    }
    qputenv("SPEECHER_AUDIO_WAV", path.toUtf8());
    return qScopeGuard([] { qunsetenv("SPEECHER_AUDIO_WAV"); });
}
#endif

} // namespace


class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

#ifdef SPEECHER_WITH_YDOTOOL
    void ydotoolEnableStepUnlocksOnlyAfterAPassedTest()
    {
        // The fake stands in for the ydotoold daemon: it types into the
        // dialog's focused test field.
        QDialog *dialog = nullptr;
        const auto fakeType = [&dialog](const QString &text, QString *) {
            auto *field = dialog->findChild<QLineEdit *>();
            if (field) {
                field->setText(text);
            }
            return true;
        };
        const std::unique_ptr<QDialog> owned(createYdotoolEnableDialog(nullptr, fakeType));
        dialog = owned.get();
        dialog->show();
        auto *run = dialog->findChild<QPushButton *>(QStringLiteral("ydotoolRunTest"));
        auto *enable = dialog->findChild<QPushButton *>(QStringLiteral("ydotoolEnable"));
        QVERIFY(run);
        QVERIFY(enable);
        // Enabling is the user's explicit step, locked until a test passes.
        QVERIFY(!enable->isEnabled());

        // Screenshot seam for UI evidence, on the pattern of the E2E rigs.
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            dialog->grab().save(grabDir + QStringLiteral("/ydotool-enable-locked.png"));
        }

        run->click();
        QTRY_VERIFY_WITH_TIMEOUT(enable->isEnabled(), 5000);
        if (!grabDir.isEmpty()) {
            dialog->grab().save(grabDir + QStringLiteral("/ydotool-enable-unlocked.png"));
        }
        QCOMPARE(dialog->result(), static_cast<int>(QDialog::Rejected));

        enable->click();
        QCOMPARE(dialog->result(), static_cast<int>(QDialog::Accepted));
    }
#endif

    void popupCanBeSizedDuringPlatformConfiguration()
    {
        auto *positioner = new SizingPopupPositioner;
        TranscriberPopup popup(positioner);

        QVERIFY(positioner->configuredSize.width() > 0);
        QVERIFY(positioner->configuredSize.height() > 0);
    }

    void positiveStatusIsNotColouredLikeALink()
    {
        // With KColorScheme the colour comes from the scheme's PositiveText;
        // without it, plain WindowText. Either way, never the link colour.
        QPalette palette;
        palette.setColor(QPalette::Link, QColor(0, 0, 200));
        palette.setColor(QPalette::WindowText, QColor(30, 30, 30));
        QVERIFY(settings::positiveTextColor(palette) != palette.color(QPalette::Link));
    }

    void popupCarriesNoSettingsPrompts()
    {
        // The overlay cannot take focus and shows while the user speaks, so a
        // system-configuration action does not belong on it.
        TranscriberPopup popup(new SizingPopupPositioner);
        QVERIFY(!popup.findChild<AccessibilityNotice *>());
        QVERIFY(!popup.findChild<QPushButton *>(QStringLiteral("enableAccessibilityButton")));
    }

    void popupKeepsWaveformAndPreviewInsideOnePill()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showListeningIndicator();
        popup.show();
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *preview = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        QVERIFY(pill && preview && waveform);
        QVERIFY(pill->isVisible());
        QVERIFY(waveform->isVisible());
        QVERIFY(!preview->isVisible());

        popup.setPreview(QStringLiteral("hello there"));
        // The transcript line sits above a compact waveform strip, both
        // centred on the capsule's vertical axis.
        QTRY_VERIFY(preview->mapTo(pill, QPoint()).y() + preview->height()
                    <= waveform->mapTo(pill, QPoint()).y());
        QVERIFY(preview->isVisible());
        const QRect waveRect(waveform->mapTo(pill, QPoint()), waveform->size());
        const QRect textRect(preview->mapTo(pill, QPoint()), preview->size());
        QVERIFY(pill->rect().contains(waveRect));
        QVERIFY(pill->rect().contains(textRect));
        QVERIFY(textRect.bottom() < waveRect.top());
        QVERIFY(qAbs(waveRect.center().x() - textRect.center().x()) <= 1);
        QVERIFY(waveform->height() < 48);

        popup.hidePreview();
        QVERIFY(!preview->isVisible());
        QVERIFY(pill->isVisible());
        QVERIFY(waveform->isVisible());
    }

    void popupContainsLargeFontStatusAndReceipt()
    {
        const QFont originalFont = QApplication::font();
        QFont largeFont = originalFont;
        largeFont.setPointSizeF(std::max(48.0, originalFont.pointSizeF() * 3));
        QApplication::setFont(largeFont);
        const auto restoreFont = qScopeGuard([originalFont] {
            QApplication::setFont(originalFont);
        });

        TranscriberPopup popup(new SizingPopupPositioner);
        popup.show();
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *waveform = popup.findChild<WaveformWidget *>();
        QVERIFY(pill && waveform);

        const auto verifyContained = [&] {
            QCoreApplication::processEvents();
            const QRect waveformRect(waveform->mapTo(pill, QPoint()), waveform->size());
            const QString geometry = QStringLiteral("pill=%1,%2 %3x%4 waveform=%5,%6 %7x%8")
                                         .arg(pill->x()).arg(pill->y())
                                         .arg(pill->width()).arg(pill->height())
                                         .arg(waveformRect.x()).arg(waveformRect.y())
                                         .arg(waveformRect.width()).arg(waveformRect.height());
            QVERIFY2(pill->rect().contains(waveformRect), qPrintable(geometry));
            QVERIFY(popup.sizeHint().height() >= pill->height() + 4);
        };

        popup.setSessionState(DictationState::Stopping);
        verifyContained();
        popup.showMessage(QStringLiteral("Input sent"), PopupOutcome::Inserted);
        verifyContained();
        popup.showOAuthRefreshIndicator();
        verifyContained();
    }

    void popupTrimsThePreviewFromTheFrontWithAnEllipsis()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showListeningIndicator();
        const QString spoken = QStringLiteral("start of a very long sentence ")
            + QStringLiteral("more words in the middle ").repeated(8)
            + QStringLiteral("the very last words");
        popup.setPreview(spoken);

        auto *preview = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        QVERIFY(preview);
        QVERIFY(preview->text().startsWith(QStringLiteral("…")));
        QVERIFY(preview->text().endsWith(QStringLiteral("the very last words")));
        const QFontMetrics metrics(preview->font());
        // One line capped at the popup's preview width (kMaxPreviewWidth).
        QVERIFY(metrics.horizontalAdvance(preview->text()) <= 440);

        // A short preview is shown whole, with nothing implied before it.
        popup.setPreview(QStringLiteral("short preview"));
        QCOMPARE(preview->text(), QStringLiteral("short preview"));
    }

    void popupErrorHugsAShortMessage()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showErrorMessage(QStringLiteral("Microphone unavailable"));
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        QVERIFY(pill);
        // The capsule sizes to the one short line instead of the full 520px
        // wrap width plus padding.
        QVERIFY(pill->sizeHint().width() < 520);

        auto *preview = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        QVERIFY(preview);
        const QFontMetrics metrics(preview->font());
        const int textWidth = metrics.horizontalAdvance(QStringLiteral("Microphone unavailable"));
        QVERIFY(preview->width() >= textWidth && preview->width() <= textWidth + 1);
        // One line, not a word broken onto a second.
        QCOMPARE(preview->heightForWidth(preview->width()), preview->heightForWidth(10000));
    }

    void popupRepositionsAfterShowingALongError()
    {
        TranscriberPopup popup;
        popup.showPopup(0);
        const int initialHeight = popup.height();
        popup.showErrorMessage(QStringLiteral(
            "Microphone access is off. Allow Speecher under Privacy & Security > "
            "Microphone, then try again."));

        const QScreen *screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        QVERIFY(popup.height() > initialHeight);
        const int clearance = screen->availableGeometry().bottom() - popup.geometry().bottom();
        QVERIFY(screen->availableGeometry().contains(popup.geometry()));
        QVERIFY2(clearance >= 28,
                 qPrintable(QStringLiteral("clearance=%1 popup=%2x%3 hint=%4x%5")
                                .arg(clearance)
                                .arg(popup.width()).arg(popup.height())
                                .arg(popup.sizeHint().width()).arg(popup.sizeHint().height())));
    }

    void popupStaysBottomAnchoredWhilePreviewHeightChanges()
    {
        // A large font exaggerates every height change, so a popup that grew
        // downward instead of upward would leave the screen's bottom margin.
        const QFont originalFont = QApplication::font();
        QFont largeFont = originalFont;
        largeFont.setPointSizeF(std::max(48.0, originalFont.pointSizeF() * 3));
        QApplication::setFont(largeFont);
        const auto restoreFont = qScopeGuard([originalFont] {
            QApplication::setFont(originalFont);
        });

        // The real fallback positioner: it anchors the window's bottom edge,
        // so every height change must re-place the window, not just resize it.
        TranscriberPopup popup;
        popup.showListeningIndicator();
        popup.showPopup(0);
        QCoreApplication::processEvents();
        const QScreen *screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        const int anchoredBottom = popup.geometry().bottom();
        QVERIFY(screen->availableGeometry().contains(popup.geometry()));

        const auto verifyAnchored = [&] {
            QCoreApplication::processEvents();
            const QString geometry = QStringLiteral("popup=%1,%2 %3x%4 anchored=%5")
                                         .arg(popup.x()).arg(popup.y())
                                         .arg(popup.width()).arg(popup.height())
                                         .arg(anchoredBottom);
            // Fontless offscreen backends can make the large status text wider
            // than the virtual screen; this test checks vertical anchoring.
            QVERIFY2(popup.geometry().top() >= screen->availableGeometry().top(),
                     qPrintable(geometry));
            QCOMPARE(popup.geometry().bottom(), anchoredBottom);
        };

        // The live preview grows the capsule upward.
        popup.setPreview(QStringLiteral("the very last words"));
        verifyAnchored();
        // Words go away for "Transcribing…" and the popup shrinks back.
        popup.setSessionState(DictationState::Stopping);
        verifyAnchored();
        // The refinement preview grows it again over the "Refining…" strip.
        popup.setRefining(true);
        popup.setRefinementPreview(QStringLiteral("the very last words"));
        verifyAnchored();
        // And back to the bare waveform pill.
        popup.setRefining(false);
        popup.hidePreview();
        verifyAnchored();
    }

    void popupErrorCanBeDismissedEarly()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showPopup(0);
        popup.showErrorMessage(QStringLiteral("Something went wrong"));
        auto *dismiss = popup.findChild<QPushButton *>(QStringLiteral("errorDismiss"));
        QVERIFY(dismiss);
        QVERIFY(!dismiss->isHidden());

        QSignalSpy dismissed(&popup, &TranscriberPopup::errorDismissed);
        dismiss->click();
        QCOMPARE(dismissed.count(), 1);
        QVERIFY(popup.isHidden());

        // The chip belongs to errors only; a live preview must not carry it.
        popup.showPopup(0);
        popup.setPreview(QStringLiteral("words again"));
        QVERIFY(dismiss->isHidden());
    }

    void popupErrorOffersItsFixAndWaitsWhileHovered()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showPopup(0);
        popup.showErrorMessage(QStringLiteral("Claude Voice is not signed in."),
                               QStringLiteral("Open Accounts"));
        auto *action = popup.findChild<QPushButton *>(QStringLiteral("errorAction"));
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *countdown = popup.findChild<QPropertyAnimation *>();
        QVERIFY(action && pill && countdown);
        QVERIFY(!action->isHidden());
        QCOMPARE(action->text(), QStringLiteral("Open Accounts"));

        QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
        QCoreApplication::sendEvent(pill, &enter);
        QCOMPARE(countdown->state(), QAbstractAnimation::Paused);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(pill, &leave);
        QCOMPARE(countdown->state(), QAbstractAnimation::Running);

        QSignalSpy requested(&popup, &TranscriberPopup::errorActionRequested);
        action->click();
        QCOMPARE(requested.count(), 1);
        QVERIFY(popup.isHidden());

        // An error without a fix shows no button.
        popup.showPopup(0);
        popup.showErrorMessage(QStringLiteral("Microphone unavailable"));
        QVERIFY(action->isHidden());
    }

    void popupDoesNotCarryAnErrorIntoTheNextDictation()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showPopup(0);
        popup.showErrorMessage(QStringLiteral("Microphone unavailable"));
        auto *dismiss = popup.findChild<QPushButton *>(QStringLiteral("errorDismiss"));
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *countdown = popup.findChild<QPropertyAnimation *>();
        QVERIFY(dismiss && pill && countdown);
        QVERIFY(!dismiss->isHidden());
        QCOMPARE(countdown->state(), QAbstractAnimation::Running);

        // The next dictation starts while that countdown is still draining.
        popup.showPopup(1);
        QVERIFY(dismiss->isHidden());
        QVERIFY(!pill->isHidden());
        QVERIFY(popup.findChild<QLabel *>(QStringLiteral("rawTranscript"))->isHidden());
        QCOMPARE(pill->height(), 48);
        // Left running it would hide this dictation's popup when it finished,
        // and report a dismissal against a session that had moved on.
        QCOMPARE(countdown->state(), QAbstractAnimation::Stopped);
    }

    void popupStopsTheCountdownWhenHidden()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showPopup(0);
        popup.showErrorMessage(QStringLiteral("Microphone unavailable"));
        auto *countdown = popup.findChild<QPropertyAnimation *>();
        QVERIFY(countdown);
        QCOMPARE(countdown->state(), QAbstractAnimation::Running);

        // The session hides the popup by any route, not only the chip.
        popup.hide();
        QCOMPARE(countdown->state(), QAbstractAnimation::Stopped);
    }

    void popupErrorKeepsTheDismissChipInsideTheCapsule()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        // The app's longest error, which clamps to the wrapping width.
        popup.showErrorMessage(QStringLiteral(
            "Microphone access is off. Allow Speecher under Privacy & Security > "
            "Microphone, then try again."));
        auto *pill = popup.findChild<QFrame *>(QStringLiteral("previewPill"));
        auto *dismiss = popup.findChild<QPushButton *>(QStringLiteral("errorDismiss"));
        QVERIFY(pill && dismiss);
        popup.adjustSize();
        // The chip must sit inside the painted capsule, not across its stroke.
        auto *icon = popup.findChild<QLabel *>(QStringLiteral("errorIcon"));
        QVERIFY(icon && !icon->isHidden());
        QVERIFY(pill->sizeHint().width()
                >= kPopupErrorWrapWidth + icon->sizeHint().width() + dismiss->sizeHint().width());
        QVERIFY(popup.width() >= pill->sizeHint().width());
        QVERIFY(popup.sizeHint().width() >= pill->sizeHint().width());
    }

    void popupErrorBreaksAnUnbrokenRunAtTheSharedWidth()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        popup.showErrorMessage(QStringLiteral("Could not reach ")
                               + QStringLiteral("https://example.com/") + QString(200, QLatin1Char('x')));
        auto *preview = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        QVERIFY(preview);
        QVERIFY(preview->width() <= kPopupErrorWrapWidth);
        QVERIFY(preview->heightForWidth(preview->width()) > preview->fontMetrics().lineSpacing() * 2);
    }

    void popupUsesTheApplicationFontAndNoStylesheet()
    {
        TranscriberPopup popup(new SizingPopupPositioner);
        QVERIFY(popup.styleSheet().isEmpty());
        for (const QWidget *child : popup.findChildren<QWidget *>()) {
            QVERIFY2(child->styleSheet().isEmpty(), qPrintable(child->objectName()));
        }
        auto *preview = popup.findChild<QLabel *>(QStringLiteral("rawTranscript"));
        QVERIFY(preview);
        QCOMPARE(preview->font().family(), QApplication::font().family());
        QCOMPARE(preview->font().pointSizeF(), QApplication::font().pointSizeF());
        QCOMPARE(preview->foregroundRole(), QPalette::Text);
    }

    void updateBannerSaysWhatEachStateOffers()
    {
        using State = UpdateController::State;
        const auto banner = [](State state, auto &&adjust) {
            UpdateBannerFacts facts;
            facts.state = state;
            facts.version = QStringLiteral("0.2.0");
            facts.percent = 42;
            facts.bannerVisible = true;
            adjust(facts);
            return updateBannerModel(facts);
        };
        const auto plain = [](UpdateBannerFacts &) {};

        const UpdateBannerModel available = banner(State::UpdateAvailable, plain);
        QCOMPARE(available.text, QStringLiteral("Speecher 0.2.0 is available"));
        QCOMPARE(available.action, QStringLiteral("Install and restart"));
        QCOMPARE(available.dismiss, QStringLiteral("Dismiss"));
        QVERIFY(available.later.isEmpty() && available.showInPopup);
        // Installing mid-dictation is safe: the restart waits.
        QVERIFY(banner(State::UpdateAvailable, [](auto &f) { f.dictating = true; }).actionEnabled);
        QCOMPARE(banner(State::UpdateAvailable, [](auto &f) { f.stableReplacement = true; }).text,
                 QStringLiteral("Switch to Stable Release 0.2.0 (replaces this Nightly Build)"));
        QCOMPARE(banner(State::UpdateAvailable, [](auto &f) { f.automaticDownloads = false; }).action,
                 QStringLiteral("Open release page"));

        const UpdateBannerModel downloading = banner(State::Downloading, plain);
        QCOMPARE(downloading.text, QStringLiteral("Downloading Speecher 0.2.0 (42%)"));
        QCOMPARE(downloading.progress, 42);
        QVERIFY(downloading.action.isEmpty() && downloading.dismiss.isEmpty());

        const UpdateBannerModel ready = banner(State::ReadyToRestart, plain);
        QCOMPARE(ready.text, QStringLiteral("Restart to finish updating"));
        QCOMPARE(ready.action, QStringLiteral("Restart now"));
        QCOMPARE(ready.later, QStringLiteral("Later"));
        // Mid-dictation the restart waits for the session, and says so.
        QCOMPARE(banner(State::ReadyToRestart, [](auto &f) { f.dictating = true; }).action,
                 QStringLiteral("Restart after this dictation"));
        QVERIFY(!banner(State::ReadyToRestart, [](auto &f) { f.deferred = true; }).visible);
        QVERIFY(!banner(State::RestartPending, [](auto &f) { f.deferred = true; }).visible);
        // Once restarting, the banner explains the exit even after Later.
        QVERIFY(banner(State::Restarting, [](auto &f) { f.deferred = true; }).visible);

        const UpdateBannerModel error = banner(State::Error, [](auto &f) {
            f.error = QStringLiteral("Download failed");
        });
        QCOMPARE(error.tone, UpdateBannerModel::Tone::Error);
        QCOMPARE(error.text, QStringLiteral("Download failed"));
        QCOMPARE(error.action, QStringLiteral("Try again"));
        QCOMPARE(error.dismiss, QStringLiteral("Dismiss"));
        QCOMPARE(banner(State::Error, [](auto &f) { f.manualInstallRequired = true; }).action,
                 QStringLiteral("Open release page"));
        // A retry or a browser window would take focus from the Target.
        QVERIFY(!banner(State::Error, [](auto &f) { f.dictating = true; }).actionEnabled);

        // An automatic check's failure shows once it keeps failing.
        const auto failedCheck = [&](bool repeated) {
            return banner(State::CheckFailed, [repeated](auto &f) {
                f.bannerVisible = false;
                f.repeatedCheckFailure = repeated;
            });
        };
        QVERIFY(!failedCheck(false).visible);
        QCOMPARE(failedCheck(true).text, QStringLiteral("Update check failed"));
        QCOMPARE(failedCheck(true).action, QStringLiteral("Try again"));

        // A manual check's progress and result stay in the settings window.
        QCOMPARE(banner(State::Checking, plain).text, QStringLiteral("Checking for updates…"));
        QVERIFY(!banner(State::Checking, plain).showInPopup);
        QCOMPARE(banner(State::UpToDate, plain).text, QStringLiteral("Speecher is up to date"));
        QVERIFY(!banner(State::UpToDate, plain).showInPopup);
        QVERIFY(!banner(State::UpdateAvailable, [](auto &f) { f.bannerVisible = false; }).visible);
    }

    void checkForUpdatesRowCaptionSaysWhatAClickDoes()
    {
        using State = UpdateController::State;
        const auto row = [](State state, bool manualInstall = false) {
            UpdateBannerFacts facts;
            facts.state = state;
            facts.version = QStringLiteral("0.2.0");
            facts.manualInstallRequired = manualInstall;
            return updateCheckRow(facts, UpdateChannel::Stable);
        };
        QCOMPARE(row(State::Idle).caption, QStringLiteral("Check now"));
        QCOMPARE(row(State::Idle).help,
                 QStringLiteral("Check the Stable Release feed for a newer build."));
        QCOMPARE(row(State::Checking).caption, QStringLiteral("Checking…"));
        QVERIFY(!row(State::Checking).enabled);
        QCOMPARE(row(State::UpToDate).caption, QStringLiteral("Check again"));
        QCOMPARE(row(State::UpdateAvailable).caption, QStringLiteral("Update now"));
        QCOMPARE(row(State::Downloading).caption, QStringLiteral("Downloading…"));
        QVERIFY(!row(State::Downloading).enabled);
        QCOMPARE(row(State::ReadyToRestart).caption, QStringLiteral("Restart now"));
        UpdateBannerFacts dictatingReady;
        dictatingReady.state = State::ReadyToRestart;
        dictatingReady.dictating = true;
        QCOMPARE(updateCheckRow(dictatingReady, UpdateChannel::Stable).caption,
                 QStringLiteral("Restart after this dictation"));
        QVERIFY(row(State::ReadyToRestart).enabled);
        QCOMPARE(row(State::CheckFailed).caption, QStringLiteral("Try again"));
        QCOMPARE(row(State::Error, true).caption, QStringLiteral("Open release page"));
    }

    void whatsNewBannerNamesTheBareVersion()
    {
        const WhatsNewBannerModel banner =
            whatsNewBanner(QStringLiteral("0.2.1-nightly.20260921+gabc1234"));
        QCOMPARE(banner.text, QStringLiteral("Speecher 0.2.1 is installed"));
        QCOMPARE(banner.action, QStringLiteral("See what's new"));
    }

    void previewTrimsAtWordBoundaries()
    {
        const auto fitsIn = [](int characters) {
            return [characters](const QString &text) { return text.size() <= characters; };
        };
        QCOMPARE(trimPreviewToFit(QStringLiteral("  short   preview "), fitsIn(40)),
                 QStringLiteral("short preview"));
        QCOMPARE(trimPreviewToFit(QStringLiteral("the hiring plan and then the budget"), fitsIn(21)),
                 QStringLiteral("… and then the budget"));
        // One word wider than the line keeps its end, cut on a grapheme.
        const QString trimmed = trimPreviewToFit(
            QStringLiteral("x ") + QString::fromUtf8("👩‍💻").repeated(8), fitsIn(12));
        QVERIFY(trimmed.startsWith(QStringLiteral("…")));
        QVERIFY(trimmed.endsWith(QString::fromUtf8("👩‍💻")));
        QVERIFY(trimmed.size() <= 12);
    }

    void previewTrimsCjkAtWordBoundaries()
    {
        // No spaces: the Unicode word boundaries decide where the cut falls.
        const QString text = QString::fromUtf8("今日は良い天気ですね明日も晴れるでしょう");
        const QString trimmed = trimPreviewToFit(text, [](const QString &candidate) {
            return candidate.size() <= 10;
        });
        QVERIFY(trimmed.startsWith(QStringLiteral("…")));
        QVERIFY(trimmed.endsWith(QString::fromUtf8("でしょう")));
        QVERIFY(trimmed.size() <= 10);
        // What is kept starts at one of the text's word boundaries.
        const QString kept = trimmed.mid(1).trimmed();
        QTextBoundaryFinder words(QTextBoundaryFinder::Word, text);
        words.setPosition(text.size() - kept.size());
        QVERIFY(words.isAtBoundary());
    }

    // Screenshot seam for UI evidence: the popup in each state it presents.
    void popupEvidenceGrabs()
    {
        const QString grabDir = qEnvironmentVariable("SPEECHER_TEST_GRAB_DIR");
        if (grabDir.isEmpty()) {
            QSKIP("SPEECHER_TEST_GRAB_DIR is not set");
        }
        TranscriberPopup popup(new SizingPopupPositioner);
        const auto grab = [&](const char *name) {
            // The size the platform positioner gives the window, once the
            // window system has applied it.
            popup.resize(popup.sizeHint());
            QTest::qWait(50);
            QVERIFY(popup.grab().save(grabDir + QStringLiteral("/linux-%1.png").arg(QLatin1String(name))));
        };
        popup.showPopup(1);
        popup.showListeningIndicator();
        grab("listening");
        popup.setPreview(QStringLiteral("so the hiring plan for next quarter should come before "
                                        "the budget review and then we can talk about the offsite"));
        grab("long-preview");
        popup.showMessage(QStringLiteral("Input sent"), PopupOutcome::Inserted);
        grab("receipt-inserted");
        popup.showMessage(QStringLiteral("Copied"), PopupOutcome::Copied);
        grab("receipt-copied");
        popup.showErrorMessage(QStringLiteral("Microphone unavailable"));
        grab("error-short");
        popup.showErrorMessage(QStringLiteral(
            "The transcription service rejected the request: the API key is invalid or has "
            "expired. Check the key on the Accounts page, then try again."));
        grab("error-long");
        popup.showErrorMessage(QStringLiteral("Could not reach https://example.com/")
                               + QString(160, QLatin1Char('x')));
        grab("error-unbroken");

        popup.showPopup(2);
        popup.showListeningIndicator();
        UpdateBannerFacts facts;
        facts.version = QStringLiteral("0.3.0");
        facts.bannerVisible = true;
        facts.percent = 42;
        const QList<std::pair<UpdateController::State, const char *>> states{
            {UpdateController::State::UpdateAvailable, "banner-available"},
            {UpdateController::State::Downloading, "banner-downloading"},
            {UpdateController::State::ReadyToRestart, "banner-ready"},
            {UpdateController::State::Error, "banner-error"},
        };
        facts.error = QStringLiteral("The download stopped before it finished.");
        for (const auto &[state, name] : states) {
            facts.state = state;
            popup.setUpdateBanner(updateBannerModel(facts));
            grab(name);
        }
        popup.setWhatsNewBanner(whatsNewBanner(QStringLiteral("0.3.0")), true);
        grab("banner-both");
    }

    void wordPreview()
    {
        QCOMPARE(WordPreview::lastWords(QStringLiteral(" one  two, three\nfour "), 2), QStringLiteral("three four"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("short"), 8), QStringLiteral("short"));
        QCOMPARE(WordPreview::lastWords(QString(), 8), QString());
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 1), QStringLiteral("gamma"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 0), QString());
        QCOMPARE(WordPreview::lastWords(QStringLiteral("earlier ").repeated(100000)
                                       + QString::fromUtf8("one\u00a0café\u2003🌍\n"), 2),
                 QString::fromUtf8("café 🌍"));
    }

    void themeUsesThePlatformColorSchemeHint()
    {
        Theme::apply(QStringLiteral("dark"));
        // Whatever the platform did, Theme reports it truthfully so the row
        // can say when Light and Dark are not going to do anything.
        QCOMPARE(Theme::overrideHonored(),
                 qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        if (qApp->styleHints()->colorScheme() != Qt::ColorScheme::Dark) {
            Theme::apply(QStringLiteral("system"));
            QSKIP("Platform theme does not honor color-scheme overrides");
        }
        Theme::apply(QStringLiteral("light"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Light);
        Theme::apply(QStringLiteral("system"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
    }

    void ignoredThemeChoicesReturnToSystem()
    {
        QCOMPARE(Theme::normalizedSetting(QStringLiteral("dark"), false),
                 QStringLiteral("system"));
        QCOMPARE(Theme::normalizedSetting(QStringLiteral("light"), true),
                 QStringLiteral("light"));
    }

    void accessibilityNoticeExplainsAndOffersTheFix()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *notice = new AccessibilityNotice(&window);
        layout->addWidget(notice);
        window.show();

        QVERIFY(notice->isHidden());
        notice->setCompact(true);
        QVERIFY(notice->isHidden());

        notice->setState(false, false, false);
        QVERIFY(notice->isHidden());

        notice->setState(true, false, false);
        QVERIFY(notice->isVisible());
        auto *message = notice->findChild<QLabel *>(QStringLiteral("accessibilityNoticeMessage"));
        auto *button = notice->findChild<QPushButton *>(QStringLiteral("enableAccessibilityButton"));
        QVERIFY(message);
        QVERIFY(button);
        QCOMPARE(message->text(), accessibilityNoticeText(false, true));
#if !defined(Q_OS_MACOS)
        // One user-facing name; the service name stays in the setup page's help.
        QVERIFY(!message->text().contains(QStringLiteral("AT-SPI")));
        QCOMPARE(button->text(), QStringLiteral("Enable desktop accessibility"));
#endif
        QSignalSpy requested(notice, &AccessibilityNotice::enableRequested);
        button->click();
        QCOMPARE(requested.count(), 1);

        notice->setState(true, true, false);
        QVERIFY(notice->isVisible());
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
        // macOS has no session-only grant; enabled always means permanent.
        QVERIFY(message->text().contains(QStringLiteral("only for this session")));
#endif

        notice->setState(true, true, true);
        QVERIFY(!notice->isVisible());
    }

    void gatedCheckBoxShowsUntickedAndKeepsItsValue()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("corrections"), *platform, providers);
        auto *learn = page->findChild<QCheckBox *>(QStringLiteral("correctionLearningControl"));
        QVERIFY(learn);
        AppSettings settings;
        settings.correctionLearningEnabled = true;
        page->load(settings);

        page->setCapabilities({false});
        QVERIFY(!learn->isEnabled());
        QVERIFY(!learn->isChecked());
        AppSettings draft;
        draft.correctionLearningEnabled = false;
        page->appendToDraft(draft);
        QVERIFY(draft.correctionLearningEnabled);

        page->setCapabilities({true});
        QVERIFY(learn->isEnabled());
        QVERIFY(learn->isChecked());
    }

    void targetAwareSettingsDisableWithoutAtSpi()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        // A refiner has to exist for the refinement rows to be live at all, so
        // accessibility is the only gate this test is measuring.
        providers.registerRefinementProvider(
            {QStringLiteral("openai"), QStringLiteral("Fake Refiner")},
            [](QObject *) -> TranscriptRefiner * { return nullptr; });
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> outputPage =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &output = *outputPage;
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("refinement"), *platform, providers);
        SchemaSettingsPage &refinement = *page;
        const std::unique_ptr<SchemaSettingsPage> profilesPage =
            schemaPage(QStringLiteral("writingProfiles"), *platform, providers);
        const std::unique_ptr<SchemaSettingsPage> correctionsPage =
            schemaPage(QStringLiteral("corrections"), *platform, providers);
        SchemaSettingsPage &corrections = *correctionsPage;
        auto *correctionLearning = corrections.findChild<QCheckBox *>(
            QStringLiteral("correctionLearningControl"));
        QVERIFY(correctionLearning);
        AppSettings refining = settings.snapshot();
        refining.refinement.providerId = QStringLiteral("openai");
        refinement.load(refining);
        profilesPage->load(refining);
        QCOMPARE(profilesPage->findChildren<QPushButton *>(QRegularExpression(QStringLiteral("^writingProfile_"))).size(),
                 5);

        output.setCapabilities({false});
        refinement.setCapabilities({false});
        corrections.setCapabilities({false});

        auto *terminalRule = output.findChild<QWidget *>(QStringLiteral("categoryPasteRule_terminal"));
        QVERIFY(terminalRule);
        QVERIFY(!terminalRule->isEnabled());
        // A gated collection stays readable; only adding to it stops.
        QVERIFY(output.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!output.findChild<QPushButton *>(QStringLiteral("addAppRecognitionRules"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        // The reason is on the page, not only in a tooltip, with the fix beside it.
        // A page can carry several gate notes, so pick the one this test is
        // about rather than whichever row happens to come first.
#ifdef Q_OS_MACOS
        const QString gateWords = QStringLiteral("Accessibility permission");
        const QString gateAction = QStringLiteral("Open Accessibility settings");
#elif defined(Q_OS_WIN)
        // Nothing in the app can make UI Automation available, so no action.
        const QString gateWords = QStringLiteral("UI Automation");
        const QString gateAction;
#else
        const QString gateWords = QStringLiteral("desktop accessibility");
        const QString gateAction = QStringLiteral("Enable desktop accessibility");
#endif
        const auto accessibilityNote = [&gateWords](SchemaSettingsPage *page) -> QWidget * {
            for (QWidget *note : page->findChildren<QWidget *>(QStringLiteral("gateNote"))) {
                auto *text = note->findChild<QLabel *>(QStringLiteral("gateNoteText"));
                if (note->isVisibleTo(page) && text && text->text().contains(gateWords)) {
                    return note;
                }
            }
            return nullptr;
        };
        for (SchemaSettingsPage *page : {&output, &refinement, &corrections}) {
            QWidget *note = accessibilityNote(page);
            QVERIFY(note);
            auto *action = note->findChild<QPushButton *>(QStringLiteral("gateAction"));
            QCOMPARE(action ? action->text() : QString(), gateAction);
        }
        // One note for the page, however many rows the gate holds.
        QCOMPARE(output.findChildren<QWidget *>(QStringLiteral("gateNote")).size(), 1);
#ifndef Q_OS_WIN
        QSignalSpy triggered(&output, &SchemaSettingsPage::actionTriggered);
        output.findChild<QPushButton *>(QStringLiteral("gateAction"))->click();
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("enableAccessibility"));
#endif

        output.setCapabilities({true});
        refinement.setCapabilities({true});
        corrections.setCapabilities({true});
        // A row that is usable drops the note that said why it was not.
        QVERIFY(correctionLearning->toolTip().isEmpty());
        QVERIFY(terminalRule->isEnabled());
        QVERIFY(output.findChild<QPushButton *>(QStringLiteral("addAppRecognitionRules"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
        for (SchemaSettingsPage *page : {&output, &refinement, &corrections}) {
            QVERIFY(!accessibilityNote(page));
        }
    }

    void qtPanesHeadTheirCardsWithTheSchemaGroupTitles()
    {
        ApplicationController controller(true);
        QWidget parent;
        SettingsPageSet pages(&controller, &parent);
        for (const SettingsPane &pane : pages.schema().panes) {
            if (pane.layout == PaneLayout::Alternatives) {
                for (const SettingsPaneGroup &group : pane.groups) {
                    const QString id = pane.id + QLatin1Char(':') + group.view;
                    QVERIFY2(pages.page(id), qPrintable(id));
                    // The view's tab carries its title, so no header repeats it.
                    QCOMPARE(sectionLabels(*pages.page(id)), QStringList{});
                }
                continue;
            }
            QStringList titles;
            for (const SettingsPaneGroup &group : pane.groups) {
                if (!group.title.isEmpty()) {
                    titles.append(group.title);
                }
            }
            if (pane.groups.isEmpty()) {
                QVERIFY2(!pages.page(pane.id), qPrintable(pane.id));
                continue;
            }
            QVERIFY2(pages.page(pane.id), qPrintable(pane.id));
            // Release notes are What's New's first group and carry no heading.
            // The Linux shortcut editor on Dictation brings its own "Dictation
            // key" heading, which is the recorder's and not a section of the page.
            if (pane.id != QStringLiteral("whatsNew")) {
                const QWidget *recorder =
                    pages.page(pane.id)->findChild<QWidget *>(QStringLiteral("shortcutCapture"));
                QCOMPARE(sectionLabels(*pages.page(pane.id), recorder), titles);
            }
            // Every row has a control named for it, except the custom blocks
            // whose widgets carry names of their own.
            for (const SettingsPaneGroup &group : pane.groups) {
                for (const QString &row : group.rows) {
                    QVERIFY2(pages.page(pane.id)->findChild<QWidget *>(row)
                                 || row == QStringLiteral("writingProfileBehavior")
                                 || row == QStringLiteral("whatsNewNotes")
                                 || row == QStringLiteral("globalShortcut")
                                 || row == QStringLiteral("localModelBrowser"),
                             qPrintable(pane.id + QLatin1Char('/') + row));
                }
            }
        }
    }

    // Paste with picks how to paste; inserting directly is a Default paste choice.
    void defaultPasteOffersAccessibilityInsertion()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());

        auto *method = page->findChild<QComboBox *>(QStringLiteral("outputMethod"));
        QVERIFY(method);
        QCOMPARE(method->findData(QStringLiteral("direct_insert")), -1);
        auto *defaultPaste = page->findChild<QComboBox *>(QStringLiteral("globalPasteRule"));
        QVERIFY(defaultPaste);
        QVERIFY(defaultPaste->findData(QStringLiteral("direct_insert")) >= 0);
        // Choices describe what happens, not which tool does it.
        for (int index = 0; index < method->count(); ++index) {
            const QString text = method->itemText(index);
            QVERIFY2(!text.contains(QStringLiteral("ydotool"), Qt::CaseInsensitive), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("wl-copy"), Qt::CaseInsensitive), qPrintable(text));
            QVERIFY2(!text.contains(QStringLiteral("Qt")), qPrintable(text));
        }
        outputRows.refresh();
        QVERIFY2(!method->toolTip().contains(QStringLiteral("ydotool")), qPrintable(method->toolTip()));
    }

    void theVocabularyLimitFollowsTheTable()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        AppSettings settings;
        settings.vocabulary = {{QStringLiteral("Speecher")}, {QStringLiteral("Deepgram")}};
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("vocabularyEntries"));
        auto *limit = page->findChild<QLabel *>(QStringLiteral("vocabularyLimit"));
        QVERIFY(table && limit);
        QCOMPARE(limit->text(),
                 VocabularyLimit::summary({QStringLiteral("Deepgram"), QStringLiteral("Speecher")}, QStringLiteral("claude")));

        table->item(0, 0)->setText(QStringLiteral("Deepgram Nova 3"));
        QCOMPARE(limit->text(),
                 VocabularyLimit::summary({QStringLiteral("Deepgram Nova 3"),
                                           QStringLiteral("Speecher")}, QStringLiteral("claude")));
    }

    void prioritisingATermMakesItAKeyTermAtOnce()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("claude");
        for (int index = 0; index < 101; ++index) {
            settings.vocabulary.append({QStringLiteral("term%1").arg(index, 3, 10, QLatin1Char('0'))});
        }
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("vocabularyEntries"));
        QVERIFY(table);
        // The Speech column holds the pills.
        const auto badge = [table](int row, int role = BadgeDelegate::TextRole) {
            return table->item(row, 1)->data(role).toString();
        };
        QCOMPARE(badge(0), QStringLiteral("Key term"));
        QCOMPARE(badge(100), QString());

        // Giving the last term priority pulls it into the key terms and
        // pushes the 100th out, before anything is saved. Priority waits on
        // Key term.
        table->selectRow(100);
        page->findChild<QPushButton *>(QStringLiteral("editVocabularyEntries"))->click();
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        auto *keyTerm = dialog->findChild<QCheckBox *>(QStringLiteral("keyTerm"));
        auto *priority = dialog->findChild<QCheckBox *>(QStringLiteral("starred"));
        QVERIFY(keyTerm->isChecked() && priority->isEnabled());
        keyTerm->setChecked(false);
        QVERIFY(!priority->isEnabled());
        keyTerm->setChecked(true);
        priority->setChecked(true);
        acceptRecordDialog(dialog);
        QCOMPARE(badge(100), QStringLiteral("Key term"));
        QCOMPARE(badge(100, BadgeDelegate::DetailBadgeRole), QStringLiteral("Priority"));
        QCOMPARE(badge(99), QString());
    }

    void addingAVocabularyTermSurvivesTheSettingsRoundTrip()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        AppSettings settings;
        settings.vocabulary = {{QStringLiteral("Speecher")}};
        page->load(settings);
        // SettingsPageSet reloads every page from the round-tripped draft on
        // each change.
        AppSettings draft = settings;
        connect(page.get(), &SchemaSettingsPage::changed, page.get(), [&draft, &page] {
            page->appendToDraft(draft);
            const QSignalBlocker blocker(page.get());
            page->load(draft);
        });

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("vocabularyEntries"));
        auto *add = page->findChild<QPushButton *>(QStringLiteral("addVocabularyEntries"));
        QVERIFY(table && add);
        QCOMPARE(table->rowCount(), 1);

        add->click();
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("New term"));
        QPushButton *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled());
        dialog->findChild<QLineEdit *>(QStringLiteral("term"))->setText(QStringLiteral("Deepgram"));
        QVERIFY(ok->isEnabled());
        acceptRecordDialog(dialog);
        QCOMPARE(table->rowCount(), 2);
        AppSettings applied;
        page->appendToDraft(applied);
        QCOMPARE(applied.vocabulary.size(), 2);
    }

    void returnOnARecordOpensItsDialog()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        AppSettings settings;
        settings.vocabulary = {{QStringLiteral("Speecher")}};
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("vocabularyEntries"));
        QVERIFY(table);
        table->setCurrentCell(0, 1);
        QTest::keyClick(table, Qt::Key_Return);
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Speecher"));
        QCOMPARE(page->findChildren<QDialog *>(QStringLiteral("collectionRecordDialog")).size(), 1);
    }

    // Edit… opens the selected term; its profiles can be limited, but not to
    // none, and its context is kept with it.
    void aTermsContextAndProfilesAreEditedInItsDialog()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        AppSettings settings;
        settings.vocabulary = {{QStringLiteral("Kubernetes")}};
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("vocabularyEntries"));
        auto *edit = page->findChild<QPushButton *>(QStringLiteral("editVocabularyEntries"));
        QVERIFY(table && edit);
        QVERIFY(!edit->isEnabled());
        table->selectRow(0);
        QVERIFY(edit->isEnabled());
        edit->click();
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Kubernetes"));
        // Both boxes say what they do.
        QVERIFY(!dialog->findChild<QLabel *>(QStringLiteral("keyTermHelp"))->text().isEmpty());
        QVERIFY(!dialog->findChild<QLabel *>(QStringLiteral("starredHelp"))->text().isEmpty());
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("context"))
            ->setPlainText(QStringLiteral("The container platform."));
        QList<QRadioButton *> choices = dialog->findChild<QWidget *>(QStringLiteral("profiles"))
                                            ->findChildren<QRadioButton *>();
        QCOMPARE(choices.size(), 2);
        QVERIFY(choices.at(0)->isChecked());
        auto *options = dialog->findChild<QListWidget *>(QStringLiteral("profilesOptions"));
        QVERIFY(!options->isEnabled());
        choices.at(1)->setChecked(true);
        QPushButton *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(options->isEnabled());
        QVERIFY(!ok->isEnabled());
        options->item(0)->setCheckState(Qt::Checked);
        QCOMPARE(options->item(0)->text(), QStringLiteral("Work"));
        QVERIFY(ok->isEnabled());
        acceptRecordDialog(dialog);

        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Work"));
        AppSettings applied;
        page->appendToDraft(applied);
        QCOMPARE(applied.vocabulary.first().context, QStringLiteral("The container platform."));
        QCOMPARE(applied.vocabulary.first().profiles, QStringList{QStringLiteral("work")});
    }

    void aToneIsAddedAndEditedInItsDialog()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("writingProfiles"), *platform, providers);
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("openai");
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("customTones"));
        auto *add = page->findChild<QPushButton *>(QStringLiteral("addCustomTones"));
        QVERIFY(table && add);
        // Only the person's own tones are listed.
        const int builtIns = 0;
        QCOMPARE(table->rowCount(), builtIns);

        add->click();
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>(QStringLiteral("name"))->setText(QStringLiteral("Terse"));
        // Refused, the dialog stays open and says why.
        acceptRecordDialog(dialog);
        QVERIFY(dialog->isVisible());
        QCOMPARE(dialog->findChild<InlineMessage *>()->label()->text(),
                 QStringLiteral("Every tone needs an instruction."));
        QCOMPARE(table->rowCount(), builtIns);

        auto *instruction = dialog->findChild<QPlainTextEdit *>(QStringLiteral("instruction"));
        QVERIFY(instruction);
        instruction->setPlainText(QStringLiteral("Short sentences.\nNo filler."));
        acceptRecordDialog(dialog);
        QVERIFY(!dialog->isVisible());
        QCOMPARE(table->rowCount(), builtIns + 1);

        emit table->cellActivated(builtIns, 1);
        dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Terse"));
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("instruction"))
            ->setPlainText(QStringLiteral("Short sentences."));
        acceptRecordDialog(dialog);

        AppSettings applied;
        page->appendToDraft(applied);
        QCOMPARE(applied.refinement.customTones.size(), 1);
        QCOMPARE(applied.refinement.customTones.first().name, QStringLiteral("Terse"));
        QCOMPARE(applied.refinement.customTones.first().instruction, QStringLiteral("Short sentences."));
    }

    // A profile's row opens it in the record dialog, where each choice says
    // what it does; a profile the person added is named and deleted there.
    void aProfileIsEditedInItsDialog()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("writingProfiles"), *platform, providers);
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("openai");
        page->load(settings);
        const auto profiles = [&page] {
            AppSettings draft;
            page->appendToDraft(draft);
            return draft.refinement.writingProfiles;
        };

        auto *email = page->findChild<QPushButton *>(QStringLiteral("writingProfile_email"));
        QVERIFY(email);
        email->click();
        QDialog *dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Email"));
        // A built-in profile keeps its name.
        QVERIFY(!dialog->findChild<QLineEdit *>(QStringLiteral("profile")));
        QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("deleteRecord")));
        auto *cleanup = dialog->findChild<QComboBox *>(QStringLiteral("cleanup"));
        auto *cleanupHelp = dialog->findChild<QLabel *>(QStringLiteral("cleanupHelp"));
        QVERIFY(cleanup && cleanupHelp);
        QCOMPARE(cleanupHelp->text(), QStringLiteral("Also removes filler words and false starts, and adds paragraphs."));
        settings::selectData(cleanup, QStringLiteral("strong_polish"));
        QCOMPARE(cleanupHelp->text(), QStringLiteral("Also rewrites for clarity, flow and organization, keeping the facts."));
        acceptRecordDialog(dialog);
        QCOMPARE(writingProfileSettingsFor(profiles(), QStringLiteral("email")).cleanupStrength,
                 QStringLiteral("strong_polish"));
        QVERIFY(email->findChild<QLabel *>(QStringLiteral("rowDescription"))->text().startsWith(
            QStringLiteral("High cleanup, no tone.")));

        page->findChild<QPushButton *>(QStringLiteral("addWritingProfile"))->click();
        dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("New profile"));
        dialog->findChild<QLineEdit *>(QStringLiteral("profile"))->setText(QStringLiteral("Notes"));
        acceptRecordDialog(dialog);
        QCOMPARE(profiles().last().profile, QStringLiteral("custom_notes"));

        page->findChild<QPushButton *>(QStringLiteral("writingProfile_custom_notes"))->click();
        dialog = shownRecordDialog(*page);
        QVERIFY(dialog);
        dialog->findChild<QPushButton *>(QStringLiteral("deleteRecord"))->click();
        QVERIFY(!dialog->isVisible());
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("writingProfile_custom_notes")));
        QCOMPARE(profiles().size(), 5);
    }

    void undoingADeletedCorrectionPutsBackEverythingItKnew()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("corrections"), *platform, providers);
        AppSettings settings;
        settings.learnedCorrections = {
            {QStringLiteral("c-1"), QStringLiteral("speecher"), QStringLiteral("Speecher"),
             QStringLiteral("org.kde.konsole"), 1750000000000, 0.92, true, 3, 1750000900000},
            {QStringLiteral("c-2"), QStringLiteral("kay dee ee"), QStringLiteral("KDE"),
             QStringLiteral("org.mozilla.firefox"), 1749000000000, 0.71, false, 1, 1749000500000},
        };
        page->load(settings);

        auto *table = page->findChild<QTableWidget *>(QStringLiteral("learnedCorrections"));
        auto *remove = page->findChild<QPushButton *>(QStringLiteral("deleteLearnedCorrections"));
        auto *undo = page->findChild<QPushButton *>(QStringLiteral("undoDeleteLearnedCorrections"));
        QVERIFY(table && remove && undo);
        // Undo shows only while there is something to undo.
        QVERIFY(undo->isHidden());

        // The second row, so an undo that put it back first would reorder them.
        table->setCurrentCell(1, 0);
        remove->click();
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(!undo->isHidden());

        undo->click();
        QCOMPARE(table->rowCount(), 2);
        AppSettings draft;
        page->appendToDraft(draft);
        QCOMPARE(draft.learnedCorrections, settings.learnedCorrections);

        // Reloading commits whatever Delete took.
        page->load(settings);
        QVERIFY(undo->isHidden());
    }

    void theHaikuCautionComesAndGoesWithTheModel()
    {
        ProviderRegistry providers;
        providers.registerRefinementProvider(
            {QStringLiteral("anthropic"), QStringLiteral("Fake Anthropic")},
            [](QObject *) -> TranscriptRefiner * { return nullptr; });
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("refinement"), *platform, providers);
        auto *caution = page->findChild<QLabel *>(QStringLiteral("anthropicModelCaution"));
        auto *model = page->findChild<QComboBox *>(QStringLiteral("anthropicModel"));
        QVERIFY(caution && model);

        AppSettings snapshot;
        snapshot.refinement.providerId = QStringLiteral("anthropic");
        page->load(snapshot);
        QVERIFY(!caution->isVisibleTo(page.get()));

        snapshot.refinement.anthropicModel = QStringLiteral("claude-haiku-4-5");
        page->load(snapshot);
        QCOMPARE(model->currentText(), QStringLiteral("Claude Haiku 4.5"));
        QVERIFY(caution->isVisibleTo(page.get()));
        QVERIFY(caution->text().contains(QStringLiteral("instructions")));
    }

    void constructingAndReopeningSetupPreservesTheSavedSpeechEndpoint()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("endpoint"));
        ProviderRegistry providers;
        providers.registerSpeechProvider({"claude", "Claude Voice", {}},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        providers.registerSpeechProvider({"endpoint", "Custom endpoint", {}},
            [](QObject *parent) { return new EndpointSpeechTranscriber(parent); });
        for (int opening = 0; opening < 2; ++opening) {
            SpeechProviderSetupPage page(settings, providers);
            QCOMPARE(settings.speechProvider(), QStringLiteral("endpoint"));
            auto *endpoint = page.findChild<QRadioButton *>("speechProviderOption_endpoint");
            QVERIFY(endpoint && endpoint->isChecked());
            page.show();
            QCOMPARE(settings.speechProvider(), QStringLiteral("endpoint"));
        }
    }

    void setupStartsOnAServiceThatIsActuallySignedIn()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("codex"));

        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex"), QString()},
            [](QObject *parent) {
                auto *provider = new FakeSpeechTranscriber(parent);
                provider->prepareResult = {false, QStringLiteral("Sign-in required")};
                return provider;
            });
        providers.registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude Voice"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });

        SpeechProviderSetupPage setup(settings, providers);
        // The page probes when it is shown, not while the wizard builds it.
        setup.show();
        auto *claude = setup.findChild<QRadioButton *>(
            QStringLiteral("speechProviderOption_claude"));
        QVERIFY(claude);
        QVERIFY(claude->isChecked());
        QCOMPARE(settings.speechProvider(), QStringLiteral("claude"));
        QVERIFY(setup.ready());
    }

    void theTranscriptionPageChoosesThisComputerWhenNoSignInIsFound()
    {
        SettingsStore settings;
        settings.raw().clear();
        QTemporaryDir emptyCliproxyDir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), emptyCliproxyDir.path());
        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude Voice"), QString()},
            [](QObject *parent) {
                auto *provider = new FakeSpeechTranscriber(parent);
                provider->prepareResult = {false, QStringLiteral("Sign-in required")};
                return provider;
            });
        providers.registerSpeechProvider({QStringLiteral("local"), QStringLiteral("Local Model"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup local(settings, providers, store);

        SpeechProviderSetupPage setup(settings, providers, &local);
        setup.show();
        auto *here = setup.findChild<QRadioButton *>(QStringLiteral("speechProviderOption_local"));
        QVERIFY(here);
        QVERIFY(here->isChecked());
        QCOMPARE(settings.speechProvider(), QStringLiteral("local"));
    }

    void theTranscriptionPageNamesTheDeadEndWhenNothingCanTranscribe()
    {
        qputenv("SPEECHER_TEST_CLAUDE_INSTALLED", "0");
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "0");
        const auto cleanup = qScopeGuard([] {
            qunsetenv("SPEECHER_TEST_CLAUDE_INSTALLED");
            qunsetenv("SPEECHER_TEST_CODEX_INSTALLED");
        });
        SettingsStore settings;
        settings.raw().clear();
        QTemporaryDir emptyCliproxyDir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), emptyCliproxyDir.path());
        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude Voice"), QString()},
            [](QObject *parent) {
                auto *provider = new FakeSpeechTranscriber(parent);
                provider->prepareResult = {false, QStringLiteral("Sign-in required")};
                return provider;
            });
        for (const char *id : {"local", "endpoint"}) {
            providers.registerSpeechProvider({id, id, {}},
                [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        }
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup local(settings, providers, store);

        SpeechProviderSetupPage setup(settings, providers, &local);
        setup.show();
        auto *deadEnd = setup.findChild<InlineMessage *>(QStringLiteral("speechDeadEnd"));
        auto *status = setup.findChild<QLabel *>(QStringLiteral("speechProviderStatus"));
        QVERIFY(deadEnd && status);
        // Until the hardware answers, this computer is still a way out.
        QVERIFY(!deadEnd->isVisibleTo(&setup));

        // The hardware answers that no model fits. With nothing installed to
        // sign in to, the note says to get an account and install Claude Code
        // or Codex, and it replaces the status line rather than repeating it.
        HardwareSummary tiny;
        tiny.profile.systemRamBytes = 2ull * 1000 * 1000 * 1000;
        tiny.profile.availableRamBytes = 2ull * 1000 * 1000 * 1000;
        LocalSetupTestAccess::setHardware(local, tiny);
        QVERIFY(deadEnd->isVisibleTo(&setup));
        QVERIFY(!status->isVisibleTo(&setup));
        QVERIFY(!setup.ready());
        QVERIFY(deadEnd->label()->text().startsWith(
            QStringLiteral("This computer can't run a local speech model")));
        QCOMPARE(setup.blockedReason(), deadEnd->label()->text());

        // Codex appears on this machine: signing in is the missing step.
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        setup.recheck();
        QCOMPARE(deadEnd->label()->text(),
                 QStringLiteral("No ChatGPT, Claude, or CLI Proxy API sign-in was found."));
        QVERIFY(!setup.ready());

        // A machine known to be too small never defaults to this computer.
        settings.setSpeechProvider(QStringLiteral("claude"));
        SpeechProviderSetupPage reopened(settings, providers, &local);
        reopened.show();
        QVERIFY(reopened.findChild<QRadioButton *>(QStringLiteral("speechProviderOption_claude"))->isChecked());

        // A speech server the person already configured is a way out.
        settings.setSpeechProvider(QStringLiteral("endpoint"));
        SpeechProviderSetupPage withServer(settings, providers, &local);
        withServer.show();
        QVERIFY(!withServer.findChild<InlineMessage *>(QStringLiteral("speechDeadEnd"))->isVisibleTo(&withServer));
    }

    void theLocalModelCardHoldsNextUntilADownloadStarts()
    {
        // A fresh install: the only provider is Local, nothing is chosen yet.
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("local"), QStringLiteral("Local model"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        QTemporaryDir models;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        LocalSetup local(settings, providers, store);

        SpeechProviderSetupPage setup(settings, providers, &local);
        setup.show();
        auto *download = setup.findChild<QPushButton *>(QStringLiteral("speechLocalDownload"));
        auto *name = setup.findChild<QLabel *>(QStringLiteral("speechLocalModelName"));
        QVERIFY(download && name);
        QVERIFY(!setup.ready());
        QCOMPARE(name->text(), local.suggestedModel().name);
        QCOMPARE(setup.findChild<Badge *>(QStringLiteral("speechLocalModelRating"))->accessibleName(),
                 modelRatingLabel(local.suggestedModel().rating));
        QVERIFY(download->text().startsWith(QStringLiteral("Download ")));

        download->click();
        QVERIFY(store.isDownloading(local.suggestedModel().id));
        QVERIFY(setup.ready());
        QCOMPARE(setup.localModelId(), local.suggestedModel().id);
        QCOMPARE(settings.localSpeechSettings().modelId, local.suggestedModel().id);
        QVERIFY(setup.readySummary().endsWith(QStringLiteral(", on this computer")));
        local.cancelDownload(local.suggestedModel().id);
    }

    // A saved model that is not the suggestion survives reopening setup
    // whether it is absent (a cancelled download), partial or downloaded.
    void reopeningSetupKeepsTheSavedLocalModel()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("claude"));
        ProviderRegistry providers;
        for (const char *id : {"claude", "local"}) {
            providers.registerSpeechProvider({id, id, {}},
                [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        }
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup local(settings, providers, store);
        const LocalModel &chosen = *findLocalModel(QStringLiteral("cohere"));
        QVERIFY(chosen.id != local.suggestedModel().id);
        LocalSpeechSettings saved = settings.localSpeechSettings();
        saved.modelId = chosen.id;
        settings.setLocalSpeechSettings(saved);

        for (const QString &suffix : {QStringLiteral("absent"), QStringLiteral(".part"), QString()}) {
            if (suffix != QStringLiteral("absent")) {
                QFile file(store.modelPath(chosen) + suffix);
                QVERIFY(file.open(QIODevice::WriteOnly));
                QVERIFY(file.resize(suffix.isEmpty() ? chosen.sizeBytes : 7));
            }
            SpeechProviderSetupPage page(settings, providers, &local);
            page.show();
            QCOMPARE(settings.localSpeechSettings().modelId, chosen.id);
            page.chooseProvider(QStringLiteral("local"));
            QCOMPARE(page.localModelId(), chosen.id);
            QCOMPARE(settings.localSpeechSettings().modelId, chosen.id);
        }

        // Opened with Local chosen, it shows the downloaded model and is ready.
        settings.setSpeechProvider(QStringLiteral("local"));
        SpeechProviderSetupPage setup(settings, providers, &local);
        setup.show();
        QCOMPARE(settings.localSpeechSettings().modelId, chosen.id);
        QCOMPARE(setup.findChild<QLabel *>(QStringLiteral("speechLocalModelName"))->text(), chosen.name);
        QVERIFY(setup.ready());
    }

    void comparisonKeyboardSelectionChangesDownloadTarget()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        providers.registerSpeechProvider({"local", "Local model", {}},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup local(settings, providers, models);
        SpeechProviderSetupPage page(settings, providers, &local);
        page.show();
        page.findChild<QToolButton *>("speechLocalCompare")->click();
        auto *table = page.findChild<QTableWidget *>("speechLocalCompareTable");
        table->setFocus();
        QTest::keyClick(table, Qt::Key_End, Qt::ControlModifier);
        const auto expected = localModelCatalog().last().id;
        QCOMPARE(page.localModelId(), expected);
        QCOMPARE(settings.localSpeechSettings().modelId, expected);
    }

    void deletingTheModelInUseMovesDictationToAnotherDownloadedOne()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup local(settings, providers, store);
        for (const QString &id : {QStringLiteral("moonshine-small"), QStringLiteral("parakeet")}) {
            const LocalModel &model = *findLocalModel(id);
            QFile file(store.modelPath(model));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.resize(model.sizeBytes));
        }
        LocalSpeechSettings saved = settings.localSpeechSettings();
        saved.modelId = QStringLiteral("parakeet");
        settings.setLocalSpeechSettings(saved);

        QVERIFY(local.removeModel(*findLocalModel(QStringLiteral("parakeet"))));
        QCOMPARE(settings.localSpeechSettings().modelId, QStringLiteral("moonshine-small"));
    }

    void anUnreadySignInWithNoRunnerLeavesRefinementOff()
    {
        // The default, not a saved choice: OpenAI while Codex is installed.
        qputenv("SPEECHER_TEST_CODEX_INSTALLED", "1");
        const auto cleanup = qScopeGuard([] { qunsetenv("SPEECHER_TEST_CODEX_INSTALLED"); });
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        providers.registerRefinementProvider({QStringLiteral("openai"), QStringLiteral("OpenAI")}, [](QObject *parent) {
            auto *refiner = new FakeRefiner(parent);
            refiner->prepareResult = {false, QStringLiteral("Not signed in")};
            return refiner;
        });

        RefinementSetupPage page(settings, providers);
        page.show();
        QCOMPARE(settings.refinementProvider(), QStringLiteral("none"));
        QVERIFY(page.findChild<QCheckBox *>(QStringLiteral("refinementSkip"))->isChecked());
    }

    void aSavedRefinementProviderStaysThoughItsSignInIsUnready()
    {
        SettingsStore settings;
        settings.raw().clear();
        // As a person's earlier choice, or a profile seeded outside the app.
        settings.raw().setValue(QStringLiteral("refinement/provider"), QStringLiteral("anthropic"));
        ProviderRegistry providers;
        providers.registerRefinementProvider({QStringLiteral("anthropic"), QStringLiteral("Anthropic")},
                                             [](QObject *parent) {
                                                 auto *refiner = new FakeRefiner(parent);
                                                 refiner->prepareResult = {false, QStringLiteral("Not signed in")};
                                                 return refiner;
                                             });

        RefinementSetupPage page(settings, providers);
        page.show();
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));
        QVERIFY(!page.findChild<QCheckBox *>(QStringLiteral("refinementSkip"))->isChecked());
    }

    void theAssistantKeepsTheCliProxyPresetUntilItsServerIsEdited()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("endpoint"));
        settings.setCliproxyBaseUrl(QStringLiteral("http://proxy.example:8317"));
        AppSettings snapshot = settings.snapshot();
        snapshot.refinement.endpoint.preset = QStringLiteral("cliproxy");
        snapshot.refinement.endpoint.model = QStringLiteral("claude-opus-5-5");
        settings.applySnapshot(snapshot);
        ProviderRegistry providers;
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup local(settings, providers, store);

        RefinementSetupPage page(settings, providers, &local);
        auto *url = page.findChild<QLineEdit *>(QStringLiteral("refinementEndpointUrl"));
        QVERIFY(url);
        // The proxy's own address is what the form shows.
        QCOMPARE(url->text(), QStringLiteral("http://proxy.example:8317/v1"));
        emit url->editingFinished();
        QCOMPARE(settings.snapshot().refinement.endpoint.preset, QStringLiteral("cliproxy"));

        url->setText(QStringLiteral("http://localhost:8080/v1"));
        emit url->editingFinished();
        QCOMPARE(settings.snapshot().refinement.endpoint.preset, QString());
        QCOMPARE(settings.snapshot().refinement.endpoint.baseUrl, QStringLiteral("http://localhost:8080/v1"));
    }

    void connectSelectsTheServersFirstModelWhenNoneIsSaved()
    {
        SettingsStore settings;
        settings.raw().clear();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                const QByteArray body = R"({"data":[{"id":"test-model"}]})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                              + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        auto snapshot = settings.snapshot();
        snapshot.refinement.providerId = "endpoint";
        snapshot.refinement.endpoint.baseUrl = QString("http://127.0.0.1:%1").arg(server.serverPort());
        settings.applySnapshot(snapshot);
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup local(settings, providers, models);
        RefinementSetupPage page(settings, providers, &local);
        page.findChild<QPushButton *>("refinementEndpointConnect")->click();
        auto *combo = page.findChild<QComboBox *>("refinementEndpointModel");
        QTRY_COMPARE(combo->count(), 1);
        QCOMPARE(combo->currentText(), QString("test-model"));
        QCOMPARE(settings.snapshot().refinement.endpoint.model, QString("test-model"));
    }

    void schemaDoesNotTurnAnUntouchedUnreadKeyIntoAnEdit()
    {
        ProviderRegistry providers;
        const auto platform = platformComposition();
        auto page = schemaPage("refinement", *platform, providers);
        AppSettings settings;
        settings.refinement.providerId = "endpoint";
        const auto key = SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey);
        settings.unreadSecretKeys = {key};
        page->load(settings);
        page->appendToDraft(settings);
        QVERIFY(settings.unreadSecretKeys.contains(key));
    }

    void editingEndpointModelPreservesALateKey()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir models;
        LocalModelStore store(models.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup local(settings, providers, store);
        RefinementSetupPage page(settings, providers, &local);
        auto snapshot = settings.snapshot();
        snapshot.refinement.endpoint.apiKey = "late-key";
        settings.applySnapshot(snapshot);
        auto *model = page.findChild<QComboBox *>("refinementEndpointModel");
        QVERIFY(model);
        model->setEditText("chosen-model");
        QCOMPARE(settings.snapshot().refinement.endpoint.apiKey, QString("late-key"));
        QCOMPARE(settings.snapshot().refinement.endpoint.model, QString("chosen-model"));
    }

    void setupSignInSourceSwitchesToCliProxy()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("claude"));
        QTemporaryDir dir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), dir.path());
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"),
                                     QStringLiteral("claude"), QStringLiteral("token-a"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-b@example.com.json"),
                                     QStringLiteral("claude"), QStringLiteral("token-b"), valid));

        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude Voice"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });

        SpeechProviderSetupPage setup(settings, providers);
        setup.show();
        auto *useCliproxy = setup.findChild<QCheckBox *>(QStringLiteral("speechUseCliproxy"));
        auto *account = setup.findChild<QComboBox *>(QStringLiteral("speechCliproxyAccount"));
        auto *directory = setup.findChild<QLineEdit *>(QStringLiteral("speechCliproxyDir"));
        QVERIFY(useCliproxy && account && directory);
        QVERIFY(!useCliproxy->isChecked());
        QVERIFY(!account->isVisibleTo(&setup));
        QVERIFY(!directory->isVisibleTo(&setup));

        // Opt in without emitting clicked, as a keyboard toggle can; the page
        // must react to that path too.
        useCliproxy->toggle();
        QVERIFY(useCliproxy->isChecked());
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("cliproxy"));
        QVERIFY(account->isVisibleTo(&setup));
        QVERIFY(directory->isVisibleTo(&setup));
        QCOMPARE(directory->text(), dir.path());
        // Two accounts and none stored force an explicit choice.
        QCOMPARE(account->currentData().toString(), QString());
        QCOMPARE(account->count(), 3);

        // The page only reacts to user selections (activated), never its own
        // repopulation, so the test emits what a click would.
        const int chosen = account->findData(QStringLiteral("claude-b@example.com.json"));
        account->setCurrentIndex(chosen);
        QMetaObject::invokeMethod(account, "activated", Q_ARG(int, chosen));
        QCOMPARE(settings.anthropicCliproxyAccount(), QStringLiteral("claude-b@example.com.json"));

        // Unchecking restores the CLI sign-in and hides the account rows.
        useCliproxy->click();
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
        QVERIFY(!account->isVisibleTo(&setup));
    }

    void signInOptOutRestoresTheFirstSeenMode()
    {
        SettingsStore settings;
        settings.raw().clear();
        QTemporaryDir dir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), dir.path());
        settings.setSpeechProvider(QStringLiteral("codex"));
        settings.setOpenAiAuthMode(QStringLiteral("env"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));

        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        providers.registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude Voice"), QString()},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });

        SpeechProviderSetupPage setup(settings, providers);
        setup.show();
        auto *useCliproxy = setup.findChild<QCheckBox *>(QStringLiteral("speechUseCliproxy"));
        QVERIFY(useCliproxy);

        // Codex opened on an environment API key; a round trip through CLI
        // Proxy API returns to it rather than to the default.
        QVERIFY(!useCliproxy->isChecked());
        useCliproxy->click();
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("cliproxy"));
        useCliproxy->click();
        QCOMPARE(settings.openAiAuthMode(), QStringLiteral("env"));

        // Claude opened already on CLI Proxy API; opting out falls back to the
        // Claude Code sign-in.
        auto *claude = setup.findChild<QRadioButton *>(
            QStringLiteral("speechProviderOption_claude"));
        QVERIFY(claude);
        claude->click();
        QVERIFY(useCliproxy->isChecked());
        useCliproxy->click();
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("oauth"));
    }

    void speechProviderChoicesComeFromTheRegistry()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("claude"));

        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("claude"),
             QStringLiteral("Claude Voice"),
             QStringLiteral("Sign in with Claude Code, then check again.")},
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });
        providers.registerSpeechProvider(
            {QStringLiteral("codex"),
             QStringLiteral("ChatGPT Codex"),
             QStringLiteral("Sign in with the ChatGPT app or Codex CLI, then check again.")},
            [](QObject *parent) {
                auto *provider = new FakeSpeechTranscriber(parent);
                provider->prepareResult = {false, QStringLiteral("Sign-in required")};
                return provider;
            });

        SpeechProviderSetupPage setup(settings, providers);
        setup.show();
        auto *claude = setup.findChild<QRadioButton *>(
            QStringLiteral("speechProviderOption_claude"));
        auto *codex = setup.findChild<QRadioButton *>(
            QStringLiteral("speechProviderOption_codex"));
        auto *claudeStatus = setup.findChild<QLabel *>(
            QStringLiteral("speechProviderStatus_claude"));
        auto *codexStatus = setup.findChild<QLabel *>(
            QStringLiteral("speechProviderStatus_codex"));
        auto *setupHint = setup.findChild<QLabel *>(QStringLiteral("speechProviderHint"));
        auto *checkAgain = setup.findChild<QPushButton *>(QStringLiteral("speechProviderCheckAgain"));
        QVERIFY(claude && codex && claudeStatus && codexStatus);
        QVERIFY(setupHint);
        QVERIFY(checkAgain);

        // Both services are on the page, each with what its probe found.
        QVERIFY(claude->isChecked());
        QCOMPARE(claudeStatus->text(), QStringLiteral("Ready"));
        QCOMPARE(codexStatus->text(), QStringLiteral("Not signed in"));
        QVERIFY(setup.ready());
        QVERIFY(setupHint->isHidden());
        QVERIFY(checkAgain->isHidden());

        codex->click();
        QCOMPARE(settings.speechProvider(), QStringLiteral("codex"));
        QVERIFY(!setup.ready());
        QVERIFY(setupHint->text().contains(QStringLiteral("ChatGPT app")));
        QVERIFY(!setupHint->isHidden());
        QVERIFY(!checkAgain->isHidden());

        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        // The Test microphone row needs a controller, which this page lacks.
        const auto standInMicrophoneTest = [](const SettingsRow &descriptor, QWidget *parent,
                                              std::function<void()>) {
            return descriptor.id == QStringLiteral("microphoneTest")
                ? SchemaCustomRow{new QWidget(parent), {}, {}}
                : SchemaCustomRow{};
        };
        const std::unique_ptr<SchemaSettingsPage> audio =
            schemaPage(QStringLiteral("audio"), *platform, providers, standInMicrophoneTest);
        AppSettings snapshot = settings.snapshot();
        audio->load(snapshot);
        auto *settingsChoice = audio->findChild<QComboBox *>(QStringLiteral("speechProvider"));
        QVERIFY(settingsChoice);
        QCOMPARE(settingsChoice->count(), 2);
        QCOMPARE(settingsChoice->currentData().toString(), QStringLiteral("codex"));

        settingsChoice->setCurrentIndex(settingsChoice->findData(QStringLiteral("claude")));
        audio->appendToDraft(snapshot);
        QCOMPARE(snapshot.speech.providerId, QStringLiteral("claude"));
        settings.applySnapshot(snapshot);
        QCOMPARE(settings.speechProvider(), QStringLiteral("claude"));
    }

    void speechProviderSetupShowsAnEndpointProblemAsTheStatus()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setSpeechProvider(QStringLiteral("endpoint"));
        ProviderRegistry providers;
        providers.registerSpeechProvider(
            {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint"), QString()},
            [](QObject *parent) {
                auto *provider = new FakeSpeechTranscriber(parent);
                provider->prepareResult = {
                    false, QStringLiteral("Set the speech endpoint's server URL in Settings.")};
                return provider;
            });

        SpeechProviderSetupPage setup(settings, providers);
        setup.show();
        auto *status = setup.findChild<QLabel *>(QStringLiteral("speechProviderStatus"));
        QVERIFY(status);
        QTRY_COMPARE(status->text(),
                     QStringLiteral("Set the speech endpoint's server URL in Settings."));
        QCOMPARE(status->toolTip(), QString());
    }

    void completionStatusDurationLoadsAndSaves()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const SettingsSchema schema = buildSettingsSchema(qtSchemaContext(*platform, providers));
        SchemaSettingsPage page(
            {schema.section(*std::find_if(schema.pane(QStringLiteral("dictation"))->groups.cbegin(),
                                          schema.pane(QStringLiteral("dictation"))->groups.cend(),
                                          [](const SettingsPaneGroup &group) {
                                              return group.rows.contains(QStringLiteral("completionStatusDuration"));
                                          }))},
            nullptr);
        AppSettings settings;
        settings.output.completionStatusDurationMs = 1200;
        page.load(settings);

        auto *duration = page.findChild<QSpinBox *>(
            QStringLiteral("completionStatusDuration"));
        QVERIFY(duration);
        QCOMPARE(duration->value(), 1200);

        duration->setValue(650);
        page.appendToDraft(settings);
        QCOMPARE(settings.output.completionStatusDurationMs, 650);
    }

    void liveCliproxyAccountPicker()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_CLIPROXY").isEmpty()) {
            QSKIP("Live CLI Proxy picker check is opt-in");
        }
        SettingsStore settings;
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());
        for (const char *name : {"openAiCliproxyAccount", "anthropicCliproxyAccount"}) {
            auto *combo = page->findChild<QComboBox *>(QString::fromLatin1(name));
            QVERIFY2(combo, name);
            qInfo().noquote() << name << "dir=" << settings.cliproxyOauthDir();
            for (int i = 0; i < combo->count(); ++i) {
                qInfo().noquote() << "  item:" << combo->itemText(i)
                                  << "data=" << combo->itemData(i).toString();
            }
            QVERIFY2(combo->count() > 0 && combo->itemText(0) != QStringLiteral("No accounts found"),
                     qPrintable(QStringLiteral("%1 shows no accounts").arg(QLatin1String(name))));
        }
    }

    void openAiAuthStatusAlwaysResolves()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setOpenAiAuthMode(QStringLiteral("auto"));
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());
        providerRows.loadSecret();

        auto *status = page->findChild<QLabel *>(QStringLiteral("openAiAuthStatus"));
        QVERIFY(status);
        QTRY_VERIFY_WITH_TIMEOUT(!status->text().isEmpty()
                                     && status->text() != QStringLiteral("Checking…"),
                                 20000);
    }

    void providerSettingsCliproxyAccountPicker()
    {
        SettingsStore settings;
        settings.raw().clear();
        SecretStore secrets(&settings);
        QTemporaryDir dir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), dir.path());
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("token-a"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-b@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("token-b"), valid));

        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());

        auto *mode = page->findChild<QComboBox *>(QStringLiteral("openAiAuthMode"));
        QVERIFY(mode);
        QVERIFY(mode->findData(QStringLiteral("cliproxy")) >= 0);
        auto *account = page->findChild<QComboBox *>(QStringLiteral("openAiCliproxyAccount"));
        QVERIFY(account);
        QCOMPARE(account->count(), 3);
        QCOMPARE(account->currentData().toString(), QString());
        QVERIFY(!page->hasChanges(settings.snapshot()));

        // Another auth mode is chosen, so the picker is neither shown nor saved.
        QVERIFY(!account->isVisibleTo(page.get()));
        account->setCurrentIndex(account->findData(QStringLiteral("codex-b@example.com.json")));
        QVERIFY(!page->hasChanges(settings.snapshot()));
        AppSettings draft = settings.snapshot();
        page->appendToDraft(draft);
        QCOMPARE(draft.refinement.openAiCliproxyAccount, QString());

        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        page->load(settings.snapshot());
        QVERIFY(account->isVisibleTo(page.get()));
        account->setCurrentIndex(account->findData(QStringLiteral("codex-b@example.com.json")));
        QVERIFY(page->hasChanges(settings.snapshot()));
        draft = settings.snapshot();
        page->appendToDraft(draft);
        QCOMPARE(draft.refinement.openAiCliproxyAccount, QStringLiteral("codex-b@example.com.json"));
    }

    void providerSettingsPreservesSpeechAccountsInServerMode()
    {
        SettingsStore settings;
        settings.raw().clear();
        SecretStore secrets(&settings);
        QTemporaryDir dir;
        settings.raw().setValue(QStringLiteral("cliproxy/oauthDir"), dir.path());
        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        settings.setOpenAiCliproxyAccount(QStringLiteral("codex-a@example.com.json"));
        settings.setAnthropicCliproxyAccount(QStringLiteral("claude-a@example.com.json"));
        settings.setCliproxyBaseUrl(QStringLiteral("http://proxy.example:8317"));
        settings.setCliproxyApiKey(QStringLiteral("server-key"));
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token"), valid));

        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());
        auto *openAi = page->findChild<QComboBox *>(QStringLiteral("openAiCliproxyAccount"));
        auto *anthropic = page->findChild<QComboBox *>(QStringLiteral("anthropicCliproxyAccount"));
        auto *baseUrl = page->findChild<QLineEdit *>(QStringLiteral("cliproxyBaseUrl"));
        auto *apiKey = page->findChild<QLineEdit *>(QStringLiteral("cliproxyApiKey"));
        QVERIFY(openAi);
        QVERIFY(anthropic);
        QVERIFY(baseUrl);
        QVERIFY(apiKey);
        QCOMPARE(openAi->currentData().toString(), QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(anthropic->currentData().toString(), QStringLiteral("claude-a@example.com.json"));
        QVERIFY(openAi->isEnabled());
        QVERIFY(anthropic->isEnabled());
        QCOMPARE(baseUrl->text(), QStringLiteral("http://proxy.example:8317"));
        QCOMPARE(apiKey->echoMode(), QLineEdit::Password);
        QVERIFY(!page->hasChanges(settings.snapshot()));

        baseUrl->setText(QStringLiteral(" http://proxy.example:8318/// "));
        apiKey->setText(QStringLiteral("new-server-key"));
        AppSettings draft = settings.snapshot();
        page->appendToDraft(draft);
        settings.applySnapshot(draft);
        QCOMPARE(settings.openAiCliproxyAccount(), QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(settings.anthropicCliproxyAccount(), QStringLiteral("claude-a@example.com.json"));
        QCOMPARE(settings.cliproxyBaseUrl(), QStringLiteral("http://proxy.example:8318"));
        QCOMPARE(settings.cliproxyApiKey(), QStringLiteral("new-server-key"));
    }

    void providerSettingsHidesCliproxyServerCardUnlessRouted()
    {
        SettingsStore settings;
        settings.raw().clear();
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());

        auto *baseUrl = page->findChild<QLineEdit *>(QStringLiteral("cliproxyBaseUrl"));
        auto *apiKey = page->findChild<QLineEdit *>(QStringLiteral("cliproxyApiKey"));
        QVERIFY(baseUrl);
        QVERIFY(apiKey);

        // Neither provider routes through the server by default, so the card
        // stays out of the way.
        QVERIFY(!baseUrl->isVisibleTo(page.get()));
        QVERIFY(!apiKey->isVisibleTo(page.get()));

        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        page->load(settings.snapshot());
        QVERIFY(baseUrl->isVisibleTo(page.get()));
        QVERIFY(apiKey->isVisibleTo(page.get()));

        settings.setOpenAiAuthMode(QStringLiteral("auto"));
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        page->load(settings.snapshot());
        QVERIFY(baseUrl->isVisibleTo(page.get()));
        QVERIFY(apiKey->isVisibleTo(page.get()));
    }

    void applicationSettingsShowsBuiltInsAndAddsCustomRules()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        SettingsStore store;
        OutputCustomRows outputRows(store);
        const std::unique_ptr<SchemaSettingsPage> outputPage =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &page = *outputPage;
        AppSettings settings;
        settings.refinement.writingProfileOverrides = {
            {QStringLiteral("org.legacy.chat"), WritingProfile::Personal, true},
        };
        page.load(settings);
        page.setCapabilities({true});

        auto *table = page.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"));
        auto *add = page.findChild<QPushButton *>(QStringLiteral("addAppRecognitionRules"));
        QVERIFY(table);
        QVERIFY(add);
        QCOMPARE(table->rowCount(), builtInAppRecognitionRules().size() + 1);

        add->click();
        QDialog *dialog = shownRecordDialog(page);
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>(QStringLiteral("match"))->setText(QStringLiteral("com.acme.shell"));
        auto *category = dialog->findChild<QComboBox *>(QStringLiteral("category"));
        auto *profile = dialog->findChild<QComboBox *>(QStringLiteral("profile"));
        QVERIFY(category);
        QVERIFY(profile);
        category->setCurrentIndex(category->findData(QStringLiteral("terminal")));
        profile->setCurrentIndex(profile->findData(QStringLiteral("work")));
        acceptRecordDialog(dialog);
        QCOMPARE(table->rowCount(), builtInAppRecognitionRules().size() + 2);

        page.appendToDraft(settings);
        QCOMPARE(settings.appRecognitionRules.size(), 2);
        QVERIFY(settings.refinement.writingProfileOverrides.isEmpty());
        QCOMPARE(settings.appRecognitionRules.last().match, QStringLiteral("com.acme.shell"));
        QCOMPARE(settings.appRecognitionRules.last().category, AppCategory::Terminal);
        QCOMPARE(settings.appRecognitionRules.last().writingProfile, WritingProfile::Work);
    }

    void sharedSettingsRowsFollowSystemSettingsLayout()
    {
        QWidget parent;
        auto *control = new QPushButton(QStringLiteral("Control"), &parent);
        const QString description = QStringLiteral(
            "A long description whose deterministic wrap width lets the form row grow to fit descenders properly.");
        QFrame *describedRow = settings::makeRow(
            QStringLiteral("Category"), description, control, &parent);
        auto *descriptionLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowDescription"));
        auto *titleLabel = describedRow->findChild<QLabel *>(QStringLiteral("rowTitle"));
        QVERIFY(descriptionLabel);
        QVERIFY(titleLabel);
        // Title and note on the left, in reading order; the control on the right.
        QCOMPARE(titleLabel->text(), QStringLiteral("Category"));
        QCOMPARE(titleLabel->alignment(), Qt::AlignLeft | Qt::AlignVCenter);
        QVERIFY(descriptionLabel->wordWrap());
        parent.resize(600, 400);
        parent.show();
        describedRow->resize(600, describedRow->sizeHint().height());
        describedRow->layout()->activate();
        QCoreApplication::processEvents();
        QVERIFY(titleLabel->mapTo(describedRow, QPoint()).y()
                < descriptionLabel->mapTo(describedRow, QPoint()).y());
        QVERIFY(control->mapTo(describedRow, QPoint()).x()
                > titleLabel->mapTo(describedRow, QPoint(titleLabel->width(), 0)).x());
        QVERIFY(control->mapTo(describedRow, QPoint(control->width(), 0)).x()
                >= describedRow->width() - settings::rowPadding().right() - 1);

        // A check box row is FormCheckDelegate: the box carries the label and
        // the description reads under it, past the indicator.
        auto *checkBox = new QCheckBox(&parent);
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Install updates automatically"), description, checkBox, &parent);
        QCOMPARE(checkBox->text(), QStringLiteral("Install updates automatically"));
        auto *checkBoxDescription = checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
        QVERIFY(checkBoxDescription);
        QCOMPARE(checkBoxDescription->text(), description);
        QVERIFY(!checkBoxRow->findChild<QLabel *>(QStringLiteral("rowTitle")));
        checkBoxRow->show();
        checkBoxRow->resize(600, checkBoxRow->sizeHint().height());
        checkBoxRow->layout()->activate();
        QCoreApplication::processEvents();
        QVERIFY(checkBoxDescription->mapTo(checkBoxRow, QPoint()).y()
                > checkBox->mapTo(checkBoxRow, QPoint()).y());
        QVERIFY(checkBoxDescription->contentsMargins().left() > 0);
    }

    void settingsCardsFitANarrowPaneWithoutClipping()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        SchemaCustomRowFactory customRows = [](const SettingsRow &row,
                                               QWidget *parent,
                                               std::function<void()>) {
            return row.id == QStringLiteral("globalShortcut")
                ? SchemaCustomRow{new QWidget(parent), {}, {}}
                : SchemaCustomRow{};
        };
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("general"), *platform, providers, customRows);
        // The long update toggle only shows when automatic downloads exist.
        page->setCapabilities({false, true});
        page->resize(500, 700);
        page->show();
        QCoreApplication::processEvents();

        QVERIFY(page->horizontalScrollBar()->maximum() == 0);
        auto *autoInstall = page->findChild<QCheckBox *>(QStringLiteral("autoInstallUpdates"));
        QVERIFY(autoInstall);
        QVERIFY(autoInstall->isVisibleTo(page.get()));
        auto *card = page->findChild<QWidget *>(QStringLiteral("settingsCardForm"));
        QVERIFY(card);
        QVERIFY(card->width() <= page->viewport()->width());
        for (QFrame *row : page->findChildren<QFrame *>(QStringLiteral("settingsRow"))) {
            if (!row->isVisibleTo(page.get())) {
                continue;
            }
            QWidget *host = row->parentWidget();
            while (host && host->objectName() != QStringLiteral("settingsCardForm")) {
                host = host->parentWidget();
            }
            QVERIFY(host);
            const QRect inCard(row->mapTo(host, QPoint(0, 0)), row->size());
            auto *rowTitle = row->findChild<QLabel *>(QStringLiteral("rowTitle"));
            QVERIFY2(inCard.right() <= host->width(), qPrintable(rowTitle ? rowTitle->text() : row->objectName()));
            QVERIFY2(inCard.left() >= 0, qPrintable(rowTitle ? rowTitle->text() : row->objectName()));
        }
    }

    void generalSettingsHelpFitsWithoutOverlappingRows()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        SchemaCustomRowFactory customRows = [](const SettingsRow &row,
                                               QWidget *parent,
                                               std::function<void()>) {
            return row.id == QStringLiteral("globalShortcut")
                ? SchemaCustomRow{new QWidget(parent), {}, {}, true}
                : SchemaCustomRow{};
        };
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("general"), *platform, providers, customRows);
        page->resize(900, 900);
        page->show();
        QCoreApplication::processEvents();

        auto *formWidget = page->findChild<QWidget *>(QStringLiteral("settingsCardForm"));
        auto *form = qobject_cast<QFormLayout *>(formWidget ? formWidget->layout() : nullptr);
        QVERIFY(form);
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *item = form->itemAt(row, QFormLayout::SpanningRole);
            if (!item) {
                item = form->itemAt(row, QFormLayout::FieldRole);
            }
            QWidget *rowWidget = item ? item->widget() : nullptr;
            if (!rowWidget || !rowWidget->isVisibleTo(page.get())) {
                continue;
            }
            const QList<QLabel *> helpLabels = rowWidget->findChildren<QLabel *>(
                QStringLiteral("rowDescription"));
            for (QLabel *help : helpLabels) {
                // A hidden description is out of the layout and keeps a stale size.
                if (!help->isVisibleTo(page.get())) {
                    continue;
                }
                QVERIFY2(help->height() >= help->heightForWidth(help->width()),
                         qPrintable(help->text()));
                for (int nextRow = row + 1; nextRow < form->rowCount(); ++nextRow) {
                    QLayoutItem *nextItem = form->itemAt(nextRow, QFormLayout::SpanningRole);
                    if (!nextItem) {
                        nextItem = form->itemAt(nextRow, QFormLayout::FieldRole);
                    }
                    QWidget *nextWidget = nextItem ? nextItem->widget() : nullptr;
                    if (!nextWidget || !nextWidget->isVisibleTo(page.get())) {
                        continue;
                    }
                    const int helpBottom = help->mapTo(formWidget, QPoint(0, help->height())).y();
                    const int nextTop = nextWidget->mapTo(formWidget, QPoint()).y();
                    QVERIFY2(helpBottom <= nextTop, qPrintable(help->text()));
                    break;
                }
            }
        }
    }

    void fullWidthSettingsRowsShareOneLeftEdge()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        SchemaCustomRowFactory customRows = [](const SettingsRow &row,
                                               QWidget *parent,
                                               std::function<void()>) {
            if (row.id != QStringLiteral("globalShortcut")) {
                return SchemaCustomRow{};
            }
            auto *body = new QWidget(parent);
            auto *layout = new QVBoxLayout(body);
            layout->setContentsMargins(0, 0, 0, 0);
            auto *text = new QLabel(QStringLiteral("Shortcut body"), body);
            text->setObjectName(QStringLiteral("shortcutBody"));
            layout->addWidget(text);
            return SchemaCustomRow{body, {}, {}, true};
        };
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("general"), *platform, providers, customRows);
        page->resize(900, 900);
        page->show();
        QCoreApplication::processEvents();

        // The card is titled "Shortcut", so the block keeps its own "Global
        // Shortcut" heading, and the heading and body line up with row titles.
        auto *heading = page->findChild<QLabel *>(QStringLiteral("subsectionLabel"));
        auto *body = page->findChild<QLabel *>(QStringLiteral("shortcutBody"));
        QVERIFY(heading);
        QVERIFY(body);
        QVERIFY(heading->isVisibleTo(page.get()));
        QCOMPARE(heading->mapTo(page.get(), QPoint()).x(), body->mapTo(page.get(), QPoint()).x());
        QLabel *rowTitle = nullptr;
        for (QLabel *candidate : page->findChildren<QLabel *>(QStringLiteral("rowTitle"))) {
            if (candidate->isVisibleTo(page.get())) {
                rowTitle = candidate;
                break;
            }
        }
        QVERIFY(rowTitle);
        QCOMPARE(body->mapTo(page.get(), QPoint()).x(), rowTitle->mapTo(page.get(), QPoint()).x());
    }

    void settingsRowsGrowForWrappedDescriptions()
    {
        QWidget surface;
        surface.resize(520, 240);
        auto *layout = new QVBoxLayout(&surface);
        auto *control = new QPushButton(QStringLiteral("A deliberately wide control"), &surface);
        control->setFixedWidth(250);
        QFrame *row = settings::makeRow(
            QStringLiteral("Output"),
            QStringLiteral("How Speecher delivers final text after dictation has completed, "
                           "including the fallback used when the preferred delivery is missing."),
            control,
            &surface);
        layout->addWidget(row);
        layout->addStretch();

        surface.show();
        QCoreApplication::processEvents();
        auto *description = row->findChild<QLabel *>(QStringLiteral("rowDescription"));
        QVERIFY(description);
        QVERIFY(description->heightForWidth(description->width())
                > description->fontMetrics().height());
        QVERIFY(description->height() >= description->heightForWidth(description->width()));
        const QRect descriptionInRow(
            description->mapTo(row, QPoint(0, 0)), description->size());
        QVERIFY(row->rect().contains(descriptionInRow.bottomLeft()));
    }

#ifdef SPEECHER_WITH_YDOTOOL
    void outputVirtualKeyboardStatusFitsWrappedText()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &output = *page;
        output.resize(900, 668);

        auto *status = output.findChild<QLabel *>(QStringLiteral("statusText"));
        QVERIFY(status);
        status->setFixedWidth(180);
        status->setText(QStringLiteral(
            "A deliberately long virtual keyboard status that wraps across many lines. "
            "It repeats enough words to exceed every real setup status shown here. "
            "The current setup state must replace it and let the row shrink again. "
            "Extra words keep this synthetic status unambiguously taller."));
        output.show();
        QCoreApplication::processEvents();

        QVERIFY(status->heightForWidth(status->width()) > status->fontMetrics().height());
        QVERIFY(status->height() >= status->heightForWidth(status->width()));
        const int longStatusHeight = status->heightForWidth(status->width());

        outputRows.refresh();
        QCoreApplication::processEvents();

        QCOMPARE(status->minimumHeight(), status->heightForWidth(status->width()));
        QVERIFY(status->minimumHeight() < longStatusHeight);
    }
#endif

    // The test owns the microphone only while it runs: stopping it, a
    // Dictation Session starting and a saved change of device each close the
    // device; running it never starts a session, and none starts while
    // dictating.
    void microphoneTestClosesTheDeviceWhenItEnds()
    {
        ApplicationController controller(true);
        const QString savedDevice = controller.settings()->audioInputDeviceId();
        const auto restoreDevice = qScopeGuard([&] { controller.settings()->setAudioInputDeviceId(savedDevice); });
        QPointer<FakeAudioInput> input;
        MicrophoneTest test(controller, nullptr, [&input](QObject *parent) {
            input = new FakeAudioInput(parent);
            return input.data();
        });
        const auto deviceClosed = [&input] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return input.isNull();
        };

        QSignalSpy levels(&test, &MicrophoneTest::levelChanged);
        test.start(savedDevice);
        QCOMPARE(test.state(), MicrophoneTestState::Running);
        emit input->levelChanged(0.5f);
        QCOMPARE(levels.size(), 1);
        QCOMPARE(controller.stateName(), QStringLiteral("idle"));
        test.stop();
        QCOMPARE(test.state(), MicrophoneTestState::Stopped);
        QVERIFY(deviceClosed());

        test.start(savedDevice);
        emit controller.stateChanged(QStringLiteral("starting"));
        QCOMPARE(test.state(), MicrophoneTestState::Stopped);
        QVERIFY(deviceClosed());

        QVERIFY(!test.canToggle());
        test.start(savedDevice);
        QCOMPARE(test.state(), MicrophoneTestState::Stopped);
        emit controller.stateChanged(QStringLiteral("idle"));
        QVERIFY(test.canToggle());

        test.start(savedDevice);
        controller.settings()->setAudioInputDeviceId(savedDevice + QStringLiteral("-other"));
        QCOMPARE(test.state(), MicrophoneTestState::Stopped);
        QVERIFY(deviceClosed());
    }

    // A device unplugged while it opens fails inside the input's start(),
    // which then gives up with a timeout. The test reports the real reason,
    // once.
    void microphoneTestReportsAFailureWhileStartingOnce()
    {
        ApplicationController controller(true);
        MicrophoneTest test(controller, nullptr, [](QObject *parent) {
            auto *input = new FakeAudioInput(parent);
            input->startError = QStringLiteral("produced no audio");
            input->onStart = [input] {
                input->emitFailure(QStringLiteral("unplugged"));
                input->startResult = false;
            };
            return input;
        });
        QSignalSpy failures(&test, &MicrophoneTest::failed);
        test.start(QString());
        QCOMPARE(test.state(), MicrophoneTestState::Stopped);
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.first().first().toString(), QStringLiteral("unplugged"));
    }

    // Opening a device can wait in a nested event loop, where a stop or the
    // test's own destruction may arrive. The input outlives its start() call,
    // and a test stopped while starting never reports Running.
    void microphoneTestSurvivesAStopWhileTheDeviceOpens()
    {
        ApplicationController controller(true);
        std::unique_ptr<MicrophoneTest> test;
        QPointer<FakeAudioInput> input;
        bool inputSurvivedTheWait = false;
        std::function<void()> duringStart;
        const auto createInput = [&](QObject *parent) {
            input = new FakeAudioInput(parent);
            input->onStart = [&] {
                duringStart();
                QEventLoop wait;
                QTimer::singleShot(0, &wait, &QEventLoop::quit);
                wait.exec();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                inputSurvivedTheWait = !input.isNull();
            };
            return input.data();
        };
        test = std::make_unique<MicrophoneTest>(controller, nullptr, createInput);
        QList<MicrophoneTestState> states;
        connect(test.get(), &MicrophoneTest::changed, test.get(), [&] { states.append(test->state()); });

        duringStart = [&] {
            QCOMPARE(test->state(), MicrophoneTestState::Starting);
            QVERIFY(!test->canToggle());
            test->stop();
        };
        test->start(QString());
        QVERIFY(inputSurvivedTheWait);
        QCOMPARE(states, QList<MicrophoneTestState>({MicrophoneTestState::Starting, MicrophoneTestState::Stopped}));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(input.isNull());

        duringStart = [&] { test.reset(); };
        MicrophoneTest *const destroyedWhileStarting = test.get();
        destroyedWhileStarting->start(QString());
        QVERIFY(inputSurvivedTheWait);
        QVERIFY(!test);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(input.isNull());
    }

#ifdef Q_OS_LINUX
    // Leaving the page or closing the window hides the row, and choosing
    // another device on the page stops the test before the page saves.
    void microphoneTestRowStopsWhenItHidesOrTheDeviceMoves()
    {
        QTemporaryDir dir;
        const auto unsetSeam = feedMicrophoneFromTone(dir);
        ApplicationController controller(true);
        SettingsRow descriptor;
        descriptor.id = QStringLiteral("microphoneTest");
        QWidget page;
        const SchemaCustomRow row = microphoneTestRow(controller)(descriptor, &page, {});
        QVERIFY(row.widget && row.refresh);
        AppSettings draft;
        row.refresh(draft);
        page.show();
        auto *button = page.findChild<QPushButton *>(QStringLiteral("microphoneTest"));

        button->click();
        QCOMPARE(button->text(), microphoneTestCaption(MicrophoneTestState::Running));
        page.hide();
        QCOMPARE(button->text(), microphoneTestCaption(MicrophoneTestState::Stopped));

        page.show();
        button->click();
        QCOMPARE(button->text(), microphoneTestCaption(MicrophoneTestState::Running));
        draft.audio.deviceId = QStringLiteral("another-microphone");
        row.refresh(draft);
        QCOMPARE(button->text(), microphoneTestCaption(MicrophoneTestState::Stopped));
    }
#endif

    // The waveform's level mapping is Wispr Flow's: an adaptive noise floor,
    // 150ms window means, then per-frame smoothing, scaled by 5 and floored at
    // 1. Expected values come from that model, not from the implementation.
    void waveformLevelModelFollowsSpeechAndSilence()
    {
        using LevelModel = speecher::waveform::LevelModel;
        // A microphone chunk is rms * 8 clipped at 1, so room tone near
        // -50 dBFS arrives as 0.024 and speech near -26 dBFS as 0.4.
        constexpr float roomTone = 0.024f;
        constexpr float speech = 0.4f;
        constexpr int frameMs = 16;
        constexpr int chunkMs = 40;

        const auto run = [](LevelModel &model, float level, int durationMs, qint64 startMs) {
            for (int elapsed = 0; elapsed < durationMs; elapsed += frameMs) {
                if (elapsed % chunkMs < frameMs) {
                    model.addChunk(level);
                }
                model.advance(startMs + elapsed);
            }
            return startMs + durationMs;
        };

        // Steady room tone defines the floor, so the bars stay at rest.
        LevelModel model;
        qint64 now = run(model, roomTone, 1000, 0);
        QCOMPARE(model.audioScale(), 1.0f);

        // Speech sits more than the model's 20dB span above that floor, so it
        // saturates: a scale near the gain of 5.
        now = run(model, speech, 1000, now);
        QVERIFY(model.audioScale() > 4.0f);

        // Silence, which is what a muted microphone and the end of a session
        // both deliver, has to bring the bars back down.
        now = run(model, 0.0f, 1000, now);
        QCOMPARE(model.audioScale(), 1.0f);

        // One freakishly quiet chunk (a single dither bit) must not drag the
        // floor so low that ordinary room tone saturates the display.
        LevelModel clamped;
        clamped.addChunk(0.00001f);
        run(clamped, roomTone, 1000, 0);
        QVERIFY(clamped.audioScale() < 5.0f);
    }

    // The pill is Wispr Flow's 50x30 while it shows the waveform; the states
    // this port does not change keep the size they had.
    void waveformPillMatchesTheTranscriptPill()
    {
        // The pill takes the transcript pill's size in every state, so it
        // reads as the same component and never resizes mid-session. The
        // height grows with the desktop font, and a message wider than the
        // pill widens it, so only the floors are fixed here.
        speecher::WaveformWidget waveform;
        const QSize resting = waveform.size();
        QCOMPARE(resting.width(), 126);
        QVERIFY(resting.height() >= 48);

        waveform.setMessage(QStringLiteral("Input sent"));
        QVERIFY(waveform.width() >= resting.width());
        QCOMPARE(waveform.height(), resting.height());

        waveform.setMode(speecher::WaveformWidget::Mode::Waveform);
        QCOMPARE(waveform.size(), resting);
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
