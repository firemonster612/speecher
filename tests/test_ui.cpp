#include "common/test_prelude.h"
#include "common/test_doubles.h"
#include "common/test_auth.h"
#include "core/VocabularyLimit.h"
#include "ui/AccessibilityNotice.h"
#include "core/SecretStore.h"
#include "frontend/qt/OutputCustomRows.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetupFlow.h"
#endif
#include "frontend/qt/ProviderCustomRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QApplication>
#include <QDialog>
#include <QGroupBox>

#include <algorithm>
#include <memory>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleHints>
#include <QTableWidget>
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
    return std::make_unique<SchemaSettingsPage>(schema.page(id), nullptr, std::move(customRows));
}

QStringList sectionLabels(const QWidget &page)
{
    // Section titles are the headers above each card, in top-to-bottom order.
    QList<QPair<int, QString>> titles;
    for (QLabel *label : page.findChildren<QLabel *>(QStringLiteral("sectionLabel"))) {
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

        popup.setStatus(QStringLiteral("Stopping"));
        verifyContained();
        popup.showMessage(QStringLiteral("Input sent"));
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
        QCOMPARE(preview->width(), textWidth);
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
        popup.setStatus(QStringLiteral("Stopping"));
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
        QVERIFY(pill->sizeHint().width() >= 520 + dismiss->sizeHint().width());
        QVERIFY(popup.width() >= pill->sizeHint().width());
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
#ifdef Q_OS_MACOS
        QVERIFY(message->text().contains(QStringLiteral("Accessibility is off")));
        QCOMPARE(button->text(), QStringLiteral("Open settings"));
#elif defined(Q_OS_WIN)
        QVERIFY(message->text().contains(QStringLiteral("UI Automation")));
        QCOMPARE(button->text(), QStringLiteral("Unavailable"));
#else
        // One user-facing name; the service name stays in the setup page's help.
        QVERIFY(message->text().contains(QStringLiteral("Desktop accessibility")));
        QVERIFY(!message->text().contains(QStringLiteral("AT-SPI")));
        QCOMPARE(button->text(), QStringLiteral("Enable permanently"));
#endif
        QSignalSpy requested(notice, &AccessibilityNotice::enableRequested);
        button->click();
#ifdef Q_OS_WIN
        QCOMPARE(requested.count(), 0);
#else
        QCOMPARE(requested.count(), 1);
#endif

        notice->setState(true, true, false);
        QVERIFY(notice->isVisible());
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
        // macOS has no session-only grant; enabled always means permanent.
        QVERIFY(message->text().contains(QStringLiteral("only for this session")));
#endif

        notice->setState(true, true, true);
        QVERIFY(!notice->isVisible());
    }

    void targetAwareSettingsDisableWithoutAtSpi()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> outputPage =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &output = *outputPage;
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("refinement"), *platform, providers);
        SchemaSettingsPage &refinement = *page;
        const std::unique_ptr<SchemaSettingsPage> correctionsPage =
            schemaPage(QStringLiteral("corrections"), *platform, providers);
        SchemaSettingsPage &corrections = *correctionsPage;
        auto *correctionLearning = corrections.findChild<QCheckBox *>(
            QStringLiteral("correctionLearningControl"));
        QVERIFY(correctionLearning);
        refinement.load(settings.snapshot());
        auto *profileSettings = refinement.findChild<QTableWidget *>(QStringLiteral("vocabInput"));
        QVERIFY(profileSettings);
        QCOMPARE(profileSettings->rowCount(), 5);

        output.setCapabilities({false});
        refinement.setCapabilities({false});
        corrections.setCapabilities({false});

        QVERIFY(!output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(!output.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        // The reason is on the page, not only in a tooltip, with the fix beside it.
        for (SchemaSettingsPage *page : {&output, &refinement, &corrections}) {
            auto *note = page->findChild<QWidget *>(QStringLiteral("gateNote"));
            QVERIFY(note);
            QVERIFY(note->isVisibleTo(page));
            auto *text = note->findChild<QLabel *>(QStringLiteral("gateNoteText"));
            auto *action = note->findChild<QPushButton *>(QStringLiteral("gateAction"));
            QVERIFY(text && action);
#ifdef Q_OS_MACOS
            QVERIFY(text->text().contains(QStringLiteral("Accessibility permission")));
            QCOMPARE(action->text(), QStringLiteral("Open Accessibility settings"));
#elif defined(Q_OS_WIN)
            QVERIFY(text->text().contains(QStringLiteral("UI Automation")));
            QCOMPARE(action->text(), QStringLiteral("UI Automation unavailable"));
#else
            QVERIFY(text->text().contains(QStringLiteral("desktop accessibility")));
            QCOMPARE(action->text(), QStringLiteral("Enable desktop accessibility"));
#endif
        }
        // One note per gated group: the paste rules and the app recognition rules.
        QCOMPARE(output.findChildren<QWidget *>(QStringLiteral("gateNote")).size(), 2);
        QSignalSpy triggered(&output, &SchemaSettingsPage::actionTriggered);
        output.findChild<QPushButton *>(QStringLiteral("gateAction"))->click();
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("enableAccessibility"));

        output.setCapabilities({true});
        refinement.setCapabilities({true});
        corrections.setCapabilities({true});
        // A row that is usable says what it does; one that is not says why.
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));
        QVERIFY(output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(output.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
        for (SchemaSettingsPage *page : {&output, &refinement, &corrections}) {
            QVERIFY(!page->findChild<QWidget *>(QStringLiteral("gateNote"))->isVisibleTo(page));
        }
    }

    void linuxSettingsLayoutsMatchMasterAndKeepSchemaRows()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const SettingsSchema schema =
            buildSettingsSchema(qtSchemaContext(*platform, providers));
        SettingsPage refinementSchema = schema.page(QStringLiteral("refinement"));
        SettingsRow sentinel;
        sentinel.id = QStringLiteral("refinementLayoutSentinel");
        sentinel.label = QStringLiteral("Sentinel");
        sentinel.kind = RowKind::Toggle;
        refinementSchema.sections.append({QStringLiteral("Later"), QString(), {sentinel}});
        const std::unique_ptr<SchemaSettingsPage> refinement =
            std::make_unique<SchemaSettingsPage>(refinementSchema);

        for (const SettingsSection &section : refinementSchema.sections) {
            for (const SettingsRow &row : section.rows) {
                const QString control = row.id == QStringLiteral("writingProfileBehavior")
                    ? QStringLiteral("vocabInput")
                    : row.id;
                QVERIFY2(refinement->findChild<QWidget *>(control), qPrintable(control));
            }
        }
        refinement->resize(900, 668);
        refinement->show();
        QCoreApplication::processEvents();
        auto *profile = refinement->findChild<QTableWidget *>(QStringLiteral("vocabInput"));
        auto *context = refinement->findChild<QWidget *>(QStringLiteral("targetContextControl"));
        QVERIFY(profile && context);
        QCOMPARE(refinement->findChildren<QFrame *>(QStringLiteral("settingsCard")).size(), 1);
        QCOMPARE(sectionLabels(*refinement), QStringList{QStringLiteral("Refinement")});
        const int profileBottom =
            profile->mapTo(refinement->widget(), QPoint(0, profile->height())).y();
        const int contextY = context->mapTo(refinement->widget(), QPoint()).y();
        QVERIFY(profileBottom <= contextY);

        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> output =
            std::make_unique<SchemaSettingsPage>(schema.page(QStringLiteral("output")),
                                                 nullptr,
                                                 outputRows.factory());
        const bool virtualKeyboard = [&schema] {
            for (const SettingsSection &section : schema.page(QStringLiteral("output")).sections) {
                for (const SettingsRow &row : section.rows) {
                    if (row.id == QStringLiteral("virtualKeyboard")) {
                        return true;
                    }
                }
            }
            return false;
        }();
        const QStringList outputLabels{
            QStringLiteral("Delivery"),
            QStringLiteral("Paste behavior"),
            virtualKeyboard ? QStringLiteral("Clipboard & virtual keyboard")
                            : QStringLiteral("Clipboard"),
            QStringLiteral("Application recognition"),
        };
        QCOMPARE(sectionLabels(*output), outputLabels);
        output->resize(900, 668);
        output->show();
        QCoreApplication::processEvents();
        auto *globalPaste = output->findChild<QWidget *>(QStringLiteral("globalPasteRule"));
        auto *restoreClipboard =
            output->findChild<QWidget *>(QStringLiteral("restoreClipboardAfterTyping"));
        QVERIFY(globalPaste && restoreClipboard);
        QVERIFY(globalPaste->mapTo(output->widget(), QPoint()).y()
                < restoreClipboard->mapTo(output->widget(), QPoint()).y());

        for (const QString &id : {QStringLiteral("general"), QStringLiteral("output")}) {
            SchemaCustomRowFactory customRows;
            if (id == QStringLiteral("output")) {
                customRows = outputRows.factory();
            }
#ifdef Q_OS_LINUX
            if (id == QStringLiteral("general")) {
                customRows = [](const SettingsRow &row,
                                QWidget *parent,
                                std::function<void()>) {
                    return row.id == QStringLiteral("globalShortcut")
                        ? SchemaCustomRow{new QWidget(parent), {}, {}}
                        : SchemaCustomRow{};
                };
            }
#endif
            const std::unique_ptr<SchemaSettingsPage> page =
                std::make_unique<SchemaSettingsPage>(schema.page(id), nullptr, customRows);
            // Every card carries its section title; General's cards are the
            // agreed four and Output keeps its schema order plus the rules.
            const QStringList generalLabels{
#ifdef Q_OS_LINUX
                QStringLiteral("Dictation"),
                QStringLiteral("Global Shortcut"),
#else
                QStringLiteral("Appearance & behavior"),
                QStringLiteral("System"),
#endif
                QStringLiteral("Setup"),
                QStringLiteral("Updates"),
            };
            QCOMPARE(sectionLabels(*page),
                     id == QStringLiteral("general") ? generalLabels : outputLabels);
        }

        // Audio keeps one everyday card and a separate Advanced card for the
        // timing controls, in that order.
        const std::unique_ptr<SchemaSettingsPage> audio =
            std::make_unique<SchemaSettingsPage>(schema.page(QStringLiteral("audio")));
        QCOMPARE(sectionLabels(*audio),
                 (QStringList{QStringLiteral("Speech to text"), QStringLiteral("Advanced")}));
        audio->resize(900, 668);
        audio->show();
        QCoreApplication::processEvents();
        auto *silence = audio->findChild<QWidget *>(QStringLiteral("vadEnabled"));
        auto *preRoll = audio->findChild<QWidget *>(QStringLiteral("preRollMs"));
        QVERIFY(silence && preRoll);
        QVERIFY(silence->mapTo(audio->widget(), QPoint()).y()
                < preRoll->mapTo(audio->widget(), QPoint()).y());
    }

    void outputMethodsOfferAccessibilityInsertion()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());

        auto *method = page->findChild<QComboBox *>(QStringLiteral("outputMethod"));
        QVERIFY(method);
        QVERIFY(method->findData(QStringLiteral("direct_insert")) >= 0);
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
                 VocabularyLimit::summary({QStringLiteral("Deepgram"), QStringLiteral("Speecher")}));

        table->item(0, 1)->setText(QStringLiteral("Deepgram Nova 3"));
        QCOMPARE(limit->text(),
                 VocabularyLimit::summary({QStringLiteral("Deepgram Nova 3"),
                                           QStringLiteral("Speecher")}));
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
        // each change; a blank vocabulary record does not survive that trip,
        // so a reload straight after Add would take the new row back.
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
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->currentColumn(), 1);

        // Typing the term is the change that reaches the settings.
        table->item(table->currentRow(), 1)->setText(QStringLiteral("Deepgram"));
        AppSettings applied;
        page->appendToDraft(applied);
        QCOMPARE(applied.vocabulary.size(), 2);
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
        QVERIFY(!undo->isEnabled());

        table->setCurrentCell(0, 0);
        remove->click();
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(undo->isEnabled());

        undo->click();
        QCOMPARE(table->rowCount(), 2);
        AppSettings draft;
        page->appendToDraft(draft);
        QCOMPARE(draft.learnedCorrections, settings.learnedCorrections);

        // Reloading commits whatever Delete took.
        page->load(settings);
        QVERIFY(!undo->isEnabled());
    }

    void theHaikuCautionComesAndGoesWithTheModel()
    {
        SettingsStore settings;
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("providers"), *platform, providers, providerRows.factory());
        auto *caution = page->findChild<QLabel *>(QStringLiteral("anthropicModelCaution"));
        auto *model = page->findChild<QComboBox *>(QStringLiteral("anthropicModel"));
        QVERIFY(caution && model);

        AppSettings snapshot;
        page->load(snapshot);
        QVERIFY(!caution->isVisibleTo(page.get()));

        snapshot.refinement.anthropicModel = QStringLiteral("claude-haiku-4-5");
        page->load(snapshot);
        QCOMPARE(model->currentText(), QStringLiteral("Claude Haiku 4.5"));
        QVERIFY(caution->isVisibleTo(page.get()));
        QVERIFY(caution->text().contains(QStringLiteral("instructions")));
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
        auto *setupChoice = setup.findChild<QComboBox *>(QStringLiteral("speechProvider"));
        auto *setupHint = setup.findChild<QLabel *>(QStringLiteral("speechProviderHint"));
        auto *checkAgain = setup.findChild<QPushButton *>(QStringLiteral("speechProviderCheckAgain"));
        QVERIFY(setupChoice);
        QVERIFY(setupHint);
        QVERIFY(checkAgain);
        QCOMPARE(setupChoice->count(), 2);
        QVERIFY(setupHint->isHidden());
        QVERIFY(checkAgain->isHidden());
        setupChoice->setCurrentIndex(setupChoice->findData(QStringLiteral("codex")));
        QCOMPARE(settings.speechProvider(), QStringLiteral("codex"));
        QVERIFY(setupHint->text().contains(QStringLiteral("ChatGPT app")));
        QVERIFY(!setupHint->isHidden());
        QVERIFY(!checkAgain->isHidden());

        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> audio =
            schemaPage(QStringLiteral("audio"), *platform, providers);
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

    void outputCompletionStatusDurationLoadsAndSaves()
    {
        SettingsStore store;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(store);
        const std::unique_ptr<SchemaSettingsPage> output =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &page = *output;
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
        QCOMPARE(table->rowCount(), builtInAppRecognitionRules().size() + 2);
        const int row = table->rowCount() - 1;
        table->item(row, 0)->setText(QStringLiteral("com.acme.shell"));
        auto *category = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        auto *profile = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
        QVERIFY(category);
        QVERIFY(profile);
        category->setCurrentIndex(category->findData(QStringLiteral("terminal")));
        profile->setCurrentIndex(profile->findData(QStringLiteral("work")));

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

        // A check box row reads as one sentence: the sentence is the row's
        // title, the box carries no text of its own, and clicking the words
        // toggles it.
        auto *checkBox = new QCheckBox(&parent);
        const QString sentence = QStringLiteral(
            "Download the update in the background and install it the next time Speecher "
            "restarts, without asking first.");
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Updates"), sentence, checkBox, &parent);
        QVERIFY(!checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription")));
        auto *caption = checkBoxRow->findChild<QLabel *>(QStringLiteral("rowTitle"));
        QVERIFY(caption);
        QVERIFY(checkBox->text().isEmpty());
        QCOMPARE(checkBox->accessibleName(), sentence);
        QCOMPARE(caption->text(), sentence);
        QVERIFY(caption->wordWrap());
        parent.show();
        QCoreApplication::processEvents();
        QVERIFY(!checkBox->isChecked());
        QTest::mouseClick(caption, Qt::LeftButton);
        QVERIFY(checkBox->isChecked());
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

#ifdef Q_OS_LINUX
    // The Global Shortcut row this test aligns against exists only on Linux.
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

        // The card is titled "Global Shortcut" and holds only this block, so the
        // block's own header stays hidden and its body lines up with row titles.
        auto *heading = page->findChild<QLabel *>(QStringLiteral("subsectionLabel"));
        auto *body = page->findChild<QLabel *>(QStringLiteral("shortcutBody"));
        QVERIFY(heading);
        QVERIFY(body);
        QVERIFY(!heading->isVisibleTo(page.get()));
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
#endif

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

    // The waveform's level mapping is Wispr Flow's: an adaptive noise floor,
    // 150ms window means, then per-frame smoothing, scaled by 5 and floored at
    // 1. Expected values come from that model, not from the implementation.
    void waveformLevelModelFollowsSpeechAndSilence()
    {
        using LevelModel = speecher::WaveformWidget::LevelModel;
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
