#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/AudioSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QImage>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleHints>
#include <QTableWidget>

using namespace speecher::test;


class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void wordPreview()
    {
        QCOMPARE(WordPreview::lastWords(QStringLiteral(" one  two, three\nfour "), 2), QStringLiteral("three four"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("short"), 8), QStringLiteral("short"));
        QCOMPARE(WordPreview::lastWords(QString(), 8), QString());
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 1), QStringLiteral("gamma"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 0), QString());
    }

    void themeUsesThePlatformColorSchemeHint()
    {
        Theme::apply(QStringLiteral("dark"));
        if (qApp->styleHints()->colorScheme() != Qt::ColorScheme::Dark) {
            QSKIP("Platform theme does not honor color-scheme overrides");
        }
        Theme::apply(QStringLiteral("light"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Light);
        Theme::apply(QStringLiteral("system"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
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
        QVERIFY(message->text().contains(QStringLiteral("AT-SPI")));
        QCOMPARE(button->text(), QStringLiteral("Enable permanently"));
        QSignalSpy requested(notice, &AccessibilityNotice::enableRequested);
        button->click();
        QCOMPARE(requested.count(), 1);

        notice->setState(true, true, false);
        QVERIFY(notice->isVisible());
        QVERIFY(message->text().contains(QStringLiteral("only for this session")));

        notice->setState(true, true, true);
        QVERIFY(!notice->isVisible());
    }

    void targetAwareSettingsDisableWithoutAtSpi()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        OutputSettingsPage output(settings);
        ApplicationSettingsPage applications;
        RefinementSettingsPage refinement(providers);
        CorrectionsSettingsPage corrections;
        auto *correctionLearning = corrections.findChild<QCheckBox *>(
            QStringLiteral("correctionLearningControl"));
        QVERIFY(correctionLearning);
        refinement.load(settings.snapshot());
        auto *profileSettings = refinement.findChild<QTableWidget *>(QStringLiteral("vocabInput"));
        QVERIFY(profileSettings);
        QCOMPARE(profileSettings->rowCount(), 5);
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));

        output.setTargetAccessibilityAvailable(false);
        applications.setTargetAccessibilityAvailable(false);
        refinement.setTargetAccessibilityAvailable(false);
        corrections.setTargetAccessibilityAvailable(false);

        QVERIFY(!output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(!applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        output.setTargetAccessibilityAvailable(true);
        applications.setTargetAccessibilityAvailable(true);
        refinement.setTargetAccessibilityAvailable(true);
        corrections.setTargetAccessibilityAvailable(true);
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));
        QVERIFY(output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
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
            [](QObject *parent) { return new FakeSpeechTranscriber(parent); });

        SpeechProviderSetupPage setup(settings, providers);
        auto *setupChoice = setup.findChild<QComboBox *>(QStringLiteral("speechProvider"));
        auto *setupHint = setup.findChild<QLabel *>(QStringLiteral("speechProviderHint"));
        QVERIFY(setupChoice);
        QVERIFY(setupHint);
        QCOMPARE(setupChoice->count(), 2);
        setupChoice->setCurrentIndex(setupChoice->findData(QStringLiteral("codex")));
        QCOMPARE(settings.speechProvider(), QStringLiteral("codex"));
        QVERIFY(setupHint->text().contains(QStringLiteral("ChatGPT app")));

        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        AudioSettingsPage audio(*platform, providers);
        AppSettings snapshot = settings.snapshot();
        audio.load(snapshot);
        auto *settingsChoice = audio.findChild<QComboBox *>(QStringLiteral("speechProvider"));
        QVERIFY(settingsChoice);
        QCOMPARE(settingsChoice->count(), 2);
        QCOMPARE(settingsChoice->currentData().toString(), QStringLiteral("codex"));

        settingsChoice->setCurrentIndex(settingsChoice->findData(QStringLiteral("claude")));
        audio.appendToDraft(snapshot);
        QCOMPARE(snapshot.speech.providerId, QStringLiteral("claude"));
        settings.applySnapshot(snapshot);
        QCOMPARE(settings.speechProvider(), QStringLiteral("claude"));
    }

    void outputCompletionStatusDurationLoadsAndSaves()
    {
        SettingsStore store;
        OutputSettingsPage page(store);
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

    void applicationSettingsShowsBuiltInsAndAddsCustomRules()
    {
        ApplicationSettingsPage page;
        AppSettings settings;
        settings.refinement.writingProfileOverrides = {
            {QStringLiteral("org.legacy.chat"), WritingProfile::Personal, true},
        };
        page.load(settings);

        auto *table = page.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"));
        auto *add = page.findChild<QPushButton *>(QStringLiteral("addAppRecognitionRule"));
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

    void sharedSettingsRowsFollowKirigamiRiverLayout()
    {
        QWidget parent;
        auto *control = new QPushButton(QStringLiteral("Control"), &parent);
        const QString description = QStringLiteral(
            "A long description whose deterministic wrap width lets the form row grow to fit descenders properly.");
        QFrame *describedRow = settings::makeRow(
            QStringLiteral("Category"), description, control, &parent);
        auto *descriptionLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowDescription"));
        auto *titleLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowTitle"));
        auto *textCell = describedRow->findChild<QWidget *>(
            QStringLiteral("rowLabelCell"));
        QVERIFY(descriptionLabel);
        QVERIFY(titleLabel);
        QVERIFY(textCell);
        QVERIFY(descriptionLabel->wordWrap());
        QCOMPARE(titleLabel->text(), QStringLiteral("Category"));
        QCOMPARE(titleLabel->alignment(), Qt::AlignLeft | Qt::AlignVCenter);
        QCOMPARE(describedRow->layout()->indexOf(textCell), 0);
        QCOMPARE(describedRow->layout()->indexOf(control), 1);

        auto *checkBox = new QCheckBox(QStringLiteral("Short label"), &parent);
        const QString sentence = QStringLiteral("Enable the complete setting sentence");
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Setting"), sentence, checkBox, &parent);
        QVERIFY(checkBox->text().isEmpty());
        QCOMPARE(checkBox->accessibleName(), QStringLiteral("Setting"));
        QCOMPARE(checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription"))->text(),
                 sentence);
    }

    void settingsCardsRenderRoundedWithVisibleSubordinateSeparators()
    {
        QWidget surface;
        surface.resize(200, 140);
        QPalette palette = surface.palette();
        palette.setColor(QPalette::Window, QColor(16, 20, 24));
        palette.setColor(QPalette::Base, QColor(31, 36, 41));
        palette.setColor(QPalette::WindowText, QColor(239, 240, 241));
        palette.setColor(QPalette::Text, QColor(239, 240, 241));
        surface.setPalette(palette);
        surface.setAutoFillBackground(true);

        QFrame *card = settings::makeSettingsCard(&surface);
        card->setGeometry(20, 20, 160, 100);
        auto *form = qobject_cast<QFormLayout *>(card->layout());
        QVERIFY(form);
        QFrame *separator = settings::makeSeparator(card);
        form->addRow(new QLabel(QStringLiteral("First"), card));
        form->addRow(separator);
        form->addRow(new QLabel(QStringLiteral("Second"), card));
        auto *opaqueChild = new QWidget(card);
        QPalette childPalette = opaqueChild->palette();
        childPalette.setColor(QPalette::Window, QColor(200, 20, 20));
        opaqueChild->setPalette(childPalette);
        opaqueChild->setAutoFillBackground(true);
        opaqueChild->setGeometry(0, 80, card->width(), 20);
        opaqueChild->show();

        surface.show();
        QCoreApplication::processEvents();
        const QImage image = surface.grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const auto colorAt = [&image, scale](const QPoint &point) {
            return image.pixelColor(qRound(point.x() * scale), qRound(point.y() * scale));
        };

        QCOMPARE(separator->height(), 1);
        QCOMPARE(colorAt(card->geometry().topLeft()), palette.color(QPalette::Window));
        QCOMPARE(colorAt(card->geometry().topLeft() + QPoint(3, 3)),
                 palette.color(QPalette::Window));
        QCOMPARE(colorAt(card->geometry().bottomLeft()), palette.color(QPalette::Window));
        QVERIFY(colorAt(card->geometry().topLeft() + QPoint(card->width() / 2, 0))
                    != palette.color(QPalette::Window));
        const QColor cardSurface = colorAt(card->geometry().topLeft() + QPoint(20, 20));
        const QColor divider = colorAt(separator->mapTo(&surface, QPoint(20, 0)));
        QVERIFY(cardSurface.lightness() > palette.color(QPalette::Window).lightness());
        QVERIFY(divider.lightness() < cardSurface.lightness());
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
            QStringLiteral("How Speecher delivers final text after dictation has completed."),
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

    void refinementTableFillsItsSettingsCard()
    {
        ProviderRegistry providers;
        RefinementSettingsPage page(providers);
        page.resize(900, 640);
        page.show();
        QCoreApplication::processEvents();

        auto *table = page.findChild<QTableWidget *>(QStringLiteral("vocabInput"));
        QVERIFY(table);
        QWidget *card = table->parentWidget();
        while (card && card->objectName() != QStringLiteral("settingsCard")) {
            card = card->parentWidget();
        }
        QVERIFY(card);
        QCOMPARE(table->width(), card->width() - 2);
    }

    void outputVirtualKeyboardStatusFitsWrappedText()
    {
        SettingsStore settings;
        OutputSettingsPage output(settings);
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

        output.refreshControls();
        QCoreApplication::processEvents();

        QCOMPARE(status->minimumHeight(), status->heightForWidth(status->width()));
        QVERIFY(status->minimumHeight() < longStatusHeight);
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
