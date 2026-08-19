#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "core/VocabularyLimit.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "frontend/qt/OutputCustomRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleHints>
#include <QTableWidget>

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
        buildSettingsSchema(qtSchemaContext(platform, providers, QStringLiteral("Clipboard")));
    return std::make_unique<SchemaSettingsPage>(schema.page(id), nullptr, std::move(customRows));
}

} // namespace


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
#ifdef Q_OS_MACOS
        QVERIFY(message->text().contains(QStringLiteral("Accessibility is off")));
        QCOMPARE(button->text(), QStringLiteral("Open settings"));
#else
        QVERIFY(message->text().contains(QStringLiteral("AT-SPI")));
        QCOMPARE(button->text(), QStringLiteral("Enable permanently"));
#endif
        QSignalSpy requested(notice, &AccessibilityNotice::enableRequested);
        button->click();
        QCOMPARE(requested.count(), 1);

        notice->setState(true, true, false);
        QVERIFY(notice->isVisible());
#ifndef Q_OS_MACOS
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
        const std::unique_ptr<SchemaSettingsPage> applicationsPage =
            schemaPage(QStringLiteral("applications"), *platform, providers);
        SchemaSettingsPage &applications = *applicationsPage;
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
        applications.setCapabilities({false});
        refinement.setCapabilities({false});
        corrections.setCapabilities({false});

        QVERIFY(!output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(!applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        output.setCapabilities({true});
        applications.setCapabilities({true});
        refinement.setCapabilities({true});
        corrections.setCapabilities({true});
        // A row that is usable says what it does; one that is not says why.
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));
        QVERIFY(output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
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
        ProviderSettingsPage page(settings, secrets);
        page.loadModels();
        page.loadAuthModes();
        for (const char *name : {"openAiCliproxyAccount", "anthropicCliproxyAccount"}) {
            auto *combo = page.findChild<QComboBox *>(QString::fromLatin1(name));
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
        ProviderSettingsPage page(settings, secrets);
        page.loadModels();
        page.loadAuthModes();
        page.loadSecret();

        auto *status = page.findChild<QLabel *>(QStringLiteral("openAiAuthStatus"));
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

        ProviderSettingsPage page(settings, secrets);
        page.loadModels();
        page.loadAuthModes();
        auto *combo = page.findChild<QComboBox *>(QStringLiteral("openAiCliproxyAccount"));
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentData().toString(), QString());
        QVERIFY(!page.hasAuthChanges());

        combo->setCurrentIndex(combo->findData(QStringLiteral("codex-b@example.com.json")));
        QVERIFY(!page.hasAuthChanges());
        page.saveAuthModes();
        QCOMPARE(settings.openAiCliproxyAccount(), QString());

        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        page.loadAuthModes();
        combo->setCurrentIndex(combo->findData(QStringLiteral("codex-b@example.com.json")));
        QVERIFY(page.hasAuthChanges());
        page.saveAuthModes();
        QCOMPARE(settings.openAiCliproxyAccount(), QStringLiteral("codex-b@example.com.json"));
        QVERIFY(!page.hasAuthChanges());
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
        const QDateTime valid = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("codex-a@example.com.json"), QStringLiteral("codex"),
                                     QStringLiteral("codex-token"), valid));
        QVERIFY(writeCliProxyAccount(dir.path(), QStringLiteral("claude-a@example.com.json"), QStringLiteral("claude"),
                                     QStringLiteral("claude-token"), valid));

        ProviderSettingsPage page(settings, secrets);
        page.loadModels();
        page.loadAuthModes();
        auto *openAi = page.findChild<QComboBox *>(QStringLiteral("openAiCliproxyAccount"));
        auto *anthropic = page.findChild<QComboBox *>(QStringLiteral("anthropicCliproxyAccount"));
        QVERIFY(openAi);
        QVERIFY(anthropic);
        QCOMPARE(openAi->currentData().toString(), QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(anthropic->currentData().toString(), QStringLiteral("claude-a@example.com.json"));
        QVERIFY(openAi->isEnabled());
        QVERIFY(anthropic->isEnabled());
        QVERIFY(!page.hasAuthChanges());

        page.saveAuthModes();
        QCOMPARE(settings.openAiCliproxyAccount(), QStringLiteral("codex-a@example.com.json"));
        QCOMPARE(settings.anthropicCliproxyAccount(), QStringLiteral("claude-a@example.com.json"));
    }

    void applicationSettingsShowsBuiltInsAndAddsCustomRules()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> applications =
            schemaPage(QStringLiteral("applications"), *platform, providers);
        SchemaSettingsPage &page = *applications;
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
        auto *titleLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowLabelCell"));
        QVERIFY(descriptionLabel);
        QVERIFY(titleLabel);
        QVERIFY(descriptionLabel->wordWrap());
        QCOMPARE(descriptionLabel->width(),
                 qMin(descriptionLabel->fontMetrics().horizontalAdvance(description) + 8,
                      descriptionLabel->fontMetrics().averageCharWidth() * 62));
        QVERIFY(descriptionLabel->heightForWidth(descriptionLabel->width())
                > descriptionLabel->fontMetrics().height());
        const int requiredRowHeight = control->sizeHint().height()
            + settings::tightSpacing()
            + descriptionLabel->heightForWidth(descriptionLabel->width());
        QVERIFY(describedRow->minimumSizeHint().height() >= requiredRowHeight);
        QCOMPARE(titleLabel->alignment(), Qt::AlignRight | Qt::AlignVCenter);

        auto *checkBox = new QCheckBox(QStringLiteral("Short label"), &parent);
        const QString sentence = QStringLiteral("Enable the complete setting sentence");
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Setting"), sentence, checkBox, &parent);
        QCOMPARE(checkBox->text(), sentence);
        QVERIFY(!checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription")));
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
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
