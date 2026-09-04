#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "core/VocabularyLimit.h"
#include "ui/AccessibilityNotice.h"
#include "core/SecretStore.h"
#include "frontend/qt/OutputCustomRows.h"
#include "frontend/qt/ProviderCustomRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QApplication>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
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

QStringList sectionLabels(const QWidget &page)
{
    QStringList labels;
    for (QLabel *label : page.findChildren<QLabel *>(QStringLiteral("sectionLabel"))) {
        labels.append(label->text());
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

    void popupCanBeSizedDuringPlatformConfiguration()
    {
        auto *positioner = new SizingPopupPositioner;
        TranscriberPopup popup(positioner);

        QVERIFY(positioner->configuredSize.width() > 0);
        QVERIFY(positioner->configuredSize.height() > 0);
    }

#ifndef SPEECHER_WITH_KCOLORSCHEME
    void positiveStatusIsNotColouredLikeALink()
    {
        QPalette palette;
        palette.setColor(QPalette::Link, QColor(0, 0, 200));
        palette.setColor(QPalette::WindowText, QColor(30, 30, 30));
        QCOMPARE(settings::positiveTextColor(palette), palette.color(QPalette::WindowText));
    }
#endif

    void popupCarriesNoSettingsPrompts()
    {
        // The overlay cannot take focus and shows while the user speaks, so a
        // system-configuration action does not belong on it.
        TranscriberPopup popup(new SizingPopupPositioner);
        QVERIFY(!popup.findChild<AccessibilityNotice *>());
        QVERIFY(!popup.findChild<QPushButton *>(QStringLiteral("enableAccessibilityButton")));
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
        // One user-facing name; the service name stays in the setup page's help.
        QVERIFY(message->text().contains(QStringLiteral("Desktop accessibility")));
        QVERIFY(!message->text().contains(QStringLiteral("AT-SPI")));
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

        // The reason is on the page, not only in a tooltip, with the fix beside it.
        for (SchemaSettingsPage *page : {&output, &applications, &refinement, &corrections}) {
            auto *note = page->findChild<QWidget *>(QStringLiteral("gateNote"));
            QVERIFY(note);
            QVERIFY(note->isVisibleTo(page));
            auto *text = note->findChild<QLabel *>(QStringLiteral("gateNoteText"));
            auto *action = note->findChild<QPushButton *>(QStringLiteral("gateAction"));
            QVERIFY(text && action);
            QVERIFY(text->text().contains(QStringLiteral("desktop accessibility")));
            QCOMPARE(action->text(), QStringLiteral("Enable desktop accessibility"));
        }
        // One note for the whole paste-rule group, above it.
        QCOMPARE(output.findChildren<QWidget *>(QStringLiteral("gateNote")).size(), 1);
        QSignalSpy triggered(&applications, &SchemaSettingsPage::actionTriggered);
        applications.findChild<QPushButton *>(QStringLiteral("gateAction"))->click();
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("enableAccessibility"));

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
        for (SchemaSettingsPage *page : {&output, &applications, &refinement, &corrections}) {
            QVERIFY(!page->findChild<QWidget *>(QStringLiteral("gateNote"))->isVisibleTo(page));
        }
    }

    void linuxSettingsLayoutsMatchMasterAndKeepSchemaRows()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const SettingsSchema schema =
            buildSettingsSchema(qtSchemaContext(*platform, providers, QStringLiteral("Clipboard")));
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
        const QList<QLabel *> refinementLabels =
            refinement->findChildren<QLabel *>(QStringLiteral("sectionLabel"));
        QCOMPARE(refinementLabels.size(), 1);
        QCOMPARE(refinementLabels.first()->text(), QStringLiteral("Refinement"));
        const QList<QFrame *> separators =
            refinement->findChildren<QFrame *>(QStringLiteral("settingsSeparator"));
        QCOMPARE(separators.size(), 1);
        const int profileBottom =
            profile->mapTo(refinement->widget(), QPoint(0, profile->height())).y();
        const int separatorY = separators.first()->mapTo(refinement->widget(), QPoint()).y();
        const int contextY = context->mapTo(refinement->widget(), QPoint()).y();
        QVERIFY(profileBottom <= separatorY && separatorY < contextY);

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

        for (const QString &id : {QStringLiteral("general"), QStringLiteral("audio"),
                                  QStringLiteral("applications")}) {
            SchemaCustomRowFactory customRows;
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
            QCOMPARE(sectionLabels(*page).size(), 0);
        }
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

        snapshot.refinement.anthropicModel = QStringLiteral("claude-haiku-4-5-20251001");
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
        QCOMPARE(descriptionLabel->maximumWidth(),
                 qMin(descriptionLabel->fontMetrics().horizontalAdvance(description) + 8,
                      descriptionLabel->fontMetrics().averageCharWidth() * 62));
        QVERIFY(descriptionLabel->heightForWidth(descriptionLabel->maximumWidth())
                > descriptionLabel->fontMetrics().height());
        const int requiredRowHeight = control->sizeHint().height()
            + settings::tightSpacing()
            + descriptionLabel->heightForWidth(descriptionLabel->maximumWidth());
        QVERIFY(describedRow->minimumSizeHint().height() >= requiredRowHeight);
        QCOMPARE(titleLabel->alignment(), Qt::AlignRight | Qt::AlignVCenter);

        // A check box's sentence sits beside the box as wrapping text (a
        // QCheckBox cannot wrap its own), and clicking the words toggles it.
        auto *checkBox = new QCheckBox(&parent);
        const QString sentence = QStringLiteral(
            "Download the update in the background and install it the next time Speecher "
            "restarts, without asking first.");
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Updates"), sentence, checkBox, &parent);
        QVERIFY(!checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription")));
        auto *caption = checkBoxRow->findChild<QLabel *>(QStringLiteral("checkBoxCaption"));
        QVERIFY(caption);
        QVERIFY(checkBox->text().isEmpty());
        QCOMPARE(checkBox->accessibleName(), sentence);
        QCOMPARE(caption->text(), sentence);
        QVERIFY(caption->wordWrap());
        QVERIFY(caption->maximumWidth() <= caption->fontMetrics().averageCharWidth() * 62);
        QVERIFY(caption->minimumSizeHint().width() < caption->fontMetrics().horizontalAdvance(sentence));
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
            const QRect inCard(row->mapTo(card, QPoint(0, 0)), row->size());
            QVERIFY2(inCard.right() <= card->width(), qPrintable(row->objectName()));
            QVERIFY2(inCard.left() >= 0, qPrintable(row->objectName()));
        }
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
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
