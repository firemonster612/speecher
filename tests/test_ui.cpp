#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "core/VocabularyLimit.h"
#include "ui/AccessibilityNotice.h"
#include "core/SecretStore.h"
#include "frontend/qt/BindingRows.h"
#include "frontend/qt/OutputCustomRows.h"
#include "frontend/qt/ProviderCustomRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/settings/FormCard.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"

#include <QApplication>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleHints>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace speecher::test;

namespace {

// The pages are the generic renderer over the core schema, so a test builds
// them the same way the front end does.
std::unique_ptr<SchemaSettingsPage> schemaPage(const QString &id,
                                               const PlatformComposition &platform,
                                               const ProviderRegistry &providers,
                                               SchemaCustomRowFactory customRows = {})
{
    const SettingsSchema schema =
        buildSettingsSchema(qtSchemaContext(platform, providers));
    return std::make_unique<SchemaSettingsPage>(schema.page(id), nullptr, std::move(customRows));
}

template <typename T>
T *ancestorOf(QWidget *widget)
{
    for (QWidget *candidate = widget->parentWidget(); candidate; candidate = candidate->parentWidget()) {
        if (auto *match = qobject_cast<T *>(candidate)) {
            return match;
        }
    }
    return nullptr;
}

QList<settings::FormRow *> shownRows(SchemaSettingsPage &page)
{
    QList<settings::FormRow *> rows;
    for (settings::FormRow *row : page.findChildren<settings::FormRow *>()) {
        if (row->isVisibleTo(&page) && row->objectName() != QStringLiteral("gateNote")) {
            rows.append(row);
        }
    }
    return rows;
}

// The column every control's right edge must sit on, in page coordinates.
int controlRightEdge(settings::FormRow *row, QWidget *page)
{
    return row->mapTo(page, QPoint(row->width() - settings::rowHorizontalPadding(), 0)).x();
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
#ifdef Q_OS_LINUX
        SettingsStore settings;
        settings.raw().setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
        Theme::apply(settings.theme());
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
        settings.raw().remove(QStringLiteral("ui/theme"));
#else
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
#endif
    }

    void ignoredThemeChoicesReturnToSystem()
    {
        QCOMPARE(Theme::normalizedSetting(QStringLiteral("dark"), false),
                 QStringLiteral("system"));
#ifdef Q_OS_LINUX
        QCOMPARE(Theme::normalizedSetting(QStringLiteral("light"), true),
                 QStringLiteral("system"));
#else
        QCOMPARE(Theme::normalizedSetting(QStringLiteral("light"), true),
                 QStringLiteral("light"));
#endif
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
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("refinement"), *platform, providers);
        SchemaSettingsPage &refinement = *page;
        const std::unique_ptr<SchemaSettingsPage> vocabularyPage =
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
        SchemaSettingsPage &vocabulary = *vocabularyPage;
        auto *correctionLearning = vocabulary.findChild<QCheckBox *>(
            QStringLiteral("correctionLearningControl"));
        QVERIFY(correctionLearning);
        refinement.load(settings.snapshot());
        auto *profileSettings = refinement.findChild<QTableWidget *>(QStringLiteral("vocabInput"));
        QVERIFY(profileSettings);
        QCOMPARE(profileSettings->rowCount(), 5);

        output.setCapabilities({false});
        refinement.setCapabilities({false});
        vocabulary.setCapabilities({false});

        // Every row that needs a known target is off, tables and combos alike;
        // the global fallback needs none and stays usable.
        QVERIFY(!output.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!output.findChild<QTableWidget *>(QStringLiteral("applicationPasteRules"))->isEnabled());
        QVERIFY(!output.findChild<QComboBox *>(QStringLiteral("categoryPasteRule_terminal"))->isEnabled());
        QVERIFY(output.findChild<QComboBox *>(QStringLiteral("globalPasteRule"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!vocabulary.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        // The reason is on the page, not only in a tooltip, with the fix beside it.
        for (SchemaSettingsPage *page : {&output, &refinement, &vocabulary}) {
            auto *note = page->findChild<QWidget *>(QStringLiteral("gateNote"));
            QVERIFY(note);
            QVERIFY(note->isVisibleTo(page));
            auto *text = note->findChild<QLabel *>(QStringLiteral("gateNoteText"));
            auto *action = note->findChild<QPushButton *>(QStringLiteral("gateAction"));
            QVERIFY(text && action);
#ifdef Q_OS_MACOS
            QVERIFY(text->text().contains(QStringLiteral("Accessibility permission")));
            QCOMPARE(action->text(), QStringLiteral("Open Accessibility settings"));
#else
            QVERIFY(text->text().contains(QStringLiteral("desktop accessibility")));
            QCOMPARE(action->text(), QStringLiteral("Enable desktop accessibility"));
#endif
        }
        // One note for the whole per-app rules group, above it.
        QCOMPARE(output.findChildren<QWidget *>(QStringLiteral("gateNote")).size(), 1);
        QSignalSpy triggered(&output, &SchemaSettingsPage::actionTriggered);
        output.findChild<QPushButton *>(QStringLiteral("gateAction"))->click();
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("enableAccessibility"));

        output.setCapabilities({true});
        refinement.setCapabilities({true});
        vocabulary.setCapabilities({true});
        // A row that is usable says what it does; one that is not says why.
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));
        QVERIFY(output.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(output.findChild<QComboBox *>(QStringLiteral("categoryPasteRule_terminal"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(vocabulary.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
        for (SchemaSettingsPage *page : {&output, &refinement, &vocabulary}) {
            QVERIFY(!page->findChild<QWidget *>(QStringLiteral("gateNote"))->isVisibleTo(page));
        }
    }

    void everySettingsRowSitsInATitledCard()
    {
        SettingsStore settings;
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(settings);
        ProviderCustomRows providerRows(settings, secrets);
        BindingRows bindingRows;
        const SettingsSchema schema =
            buildSettingsSchema(qtSchemaContext(*platform, providers));
        const QList<std::pair<QString, SchemaCustomRowFactory>> pages{
            {QStringLiteral("general"), {}},
            {QStringLiteral("audio"), {}},
            {QStringLiteral("output"), outputRows.factory()},
            {QStringLiteral("accounts"), providerRows.factory()},
            {QStringLiteral("refinement"), {}},
            {QStringLiteral("vocabulary"), bindingRows.factory()},
        };
        for (const auto &[id, factory] : pages) {
            const SettingsPage &descriptor = schema.page(id);
            SchemaSettingsPage page(descriptor, nullptr, factory);
            for (const SettingsSection &section : descriptor.sections) {
                for (const SettingsRow &row : section.rows) {
                    const QString control = row.id == QStringLiteral("writingProfileBehavior")
                        ? QStringLiteral("vocabInput")
                        : row.id == QStringLiteral("bindingRules") ? QStringLiteral("bindingList")
                                                                    : row.id;
                    QWidget *widget = page.findChild<QWidget *>(control);
                    QVERIFY2(widget, qPrintable(control));
                    auto *formRow = ancestorOf<settings::FormRow>(widget);
                    auto *card = ancestorOf<settings::SettingsCard>(widget);
                    QVERIFY2(formRow && card, qPrintable(control));
                    QCOMPARE(card->title(), section.title);
                    QVERIFY2(!card->title().isEmpty(), qPrintable(control));
                }
            }
        }
    }

    void cardsHideWithTheirLastRowAndSeparatorsFollow()
    {
        SettingsStore settings;
        settings.raw().clear();
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());
        page->resize(900, 900);
        page->show();
        QCoreApplication::processEvents();

        // The server card has nothing to show until a sign-in routes through it.
        settings::SettingsCard *server = nullptr;
        for (settings::SettingsCard *card : page->findChildren<settings::SettingsCard *>()) {
            if (card->title() == QStringLiteral("CLI Proxy API server")) {
                server = card;
            }
        }
        QVERIFY(server);
        QVERIFY(!server->isVisibleTo(page.get()));
        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        page->load(settings.snapshot());
        QVERIFY(server->isVisibleTo(page.get()));

        // A separator only sits between two rows that are both shown.
        auto *account = page->findChild<QComboBox *>(QStringLiteral("openAiCliproxyAccount"));
        QVERIFY(account);
        auto *accountRow = ancestorOf<settings::FormRow>(account);
        auto *codex = ancestorOf<settings::SettingsCard>(account);
        QVERIFY(accountRow && codex);
        QVERIFY(accountRow->isVisibleTo(page.get()));
        int shownSeparators = 0;
        for (QFrame *separator : codex->findChildren<QFrame *>(QStringLiteral("rowSeparator"))) {
            shownSeparators += separator->isVisibleTo(page.get());
        }
        QCOMPARE(shownSeparators, 2);
        settings.setOpenAiAuthMode(QStringLiteral("auto"));
        page->load(settings.snapshot());
        QVERIFY(!accountRow->isVisibleTo(page.get()));
        shownSeparators = 0;
        for (QFrame *separator : codex->findChildren<QFrame *>(QStringLiteral("rowSeparator"))) {
            shownSeparators += separator->isVisibleTo(page.get());
        }
        QCOMPARE(shownSeparators, 1);
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
            schemaPage(QStringLiteral("vocabulary"), *platform, providers);
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
            schemaPage(QStringLiteral("refinement"), *platform, providers, providerRows.factory());
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
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
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
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
        page->load(settings.snapshot());
        providerRows.loadSecret();

        auto *status = page->findChild<QLabel *>(QStringLiteral("openAiAuthStatus"));
        QVERIFY(status);
        QTRY_VERIFY_WITH_TIMEOUT(!status->text().isEmpty()
                                     && status->text() != QStringLiteral("Checking…"),
                                 20000);
    }

    void signedOutAccountRowsGivePlainSignInDirections()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldOpenAiKey = qgetenv("OPENAI_API_KEY");
        const auto restoreEnvironment = qScopeGuard([oldHome, oldOpenAiKey] {
            oldHome.isNull() ? qunsetenv("HOME") : qputenv("HOME", oldHome);
            oldOpenAiKey.isNull() ? qunsetenv("OPENAI_API_KEY")
                                  : qputenv("OPENAI_API_KEY", oldOpenAiKey);
        });
        qputenv("HOME", QFile::encodeName(home.path()));
        qunsetenv("OPENAI_API_KEY");

        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(QStringLiteral("claude/credentialsPath"),
                                home.filePath(QStringLiteral("private/credentials.json")));
        settings.setOpenAiAuthMode(QStringLiteral("auto"));
        settings.setAnthropicAuthMode(QStringLiteral("oauth"));
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page = schemaPage(
            QStringLiteral("accounts"), *platformComposition(), providers, providerRows.factory());
        page->load(settings.snapshot());
        providerRows.loadSecret();

        auto *openAi = page->findChild<QLabel *>(QStringLiteral("openAiAuthStatus"));
        auto *claude = page->findChild<QLabel *>(QStringLiteral("anthropicAuthStatus"));
        QVERIFY(openAi);
        QVERIFY(claude);
        QTRY_COMPARE_WITH_TIMEOUT(
            openAi->text(),
            QStringLiteral("Not signed in. Sign in with the Codex app or run codex login."),
            20000);
        QCOMPARE(claude->text(),
                 QStringLiteral("Not signed in. Run claude and sign in there."));
        QCOMPARE(ancestorOf<settings::FormRow>(claude)->title(), QStringLiteral("Account"));
        for (const QLabel *status : {openAi, claude}) {
            QVERIFY(!status->text().contains(home.path()));
            QVERIFY(!status->text().contains(QStringLiteral("credential"), Qt::CaseInsensitive));
            QVERIFY(!status->text().contains(QStringLiteral("/login")));
        }
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
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
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

    void anthropicAccountStatusDescribesTheSelectedSource()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setAnthropicAuthMode(QStringLiteral("cliproxy"));
        QCOMPARE(settings.anthropicAuthMode(), QStringLiteral("cliproxy"));
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page = schemaPage(
            QStringLiteral("accounts"), *platformComposition(), providers, providerRows.factory());

        auto *status = page->findChild<QLabel *>(QStringLiteral("anthropicAuthStatus"));
        QVERIFY(status);
        QVERIFY(!status->text().isEmpty());
        page->load(settings.snapshot());
        QCOMPARE(status->text(), QStringLiteral("Uses a CLI Proxy API account."));
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
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
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
            schemaPage(QStringLiteral("accounts"), *platform, providers, providerRows.factory());
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
        SettingsStore store;
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        OutputCustomRows outputRows(store);
        const std::unique_ptr<SchemaSettingsPage> output =
            schemaPage(QStringLiteral("output"), *platform, providers, outputRows.factory());
        SchemaSettingsPage &page = *output;
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

    void formRowsPutTheControlOnTheRightAndWrapTheSubtitle()
    {
        settings::SettingsCard card(QStringLiteral("Card"), QStringLiteral("One line under it"));
        card.setFixedWidth(420);
        const QString subtitle = QStringLiteral(
            "A long subtitle whose wrap width at this card size lets the row grow to fit every "
            "line rather than clipping the last one.");
        auto *row = new settings::FormRow(QStringLiteral("Title"), subtitle, card.body());
        auto *control = new QPushButton(QStringLiteral("Control"), row);
        row->setControl(control);
        card.addRow(row);
        auto *second = new settings::FormRow(QStringLiteral("Second"), QString(), card.body());
        auto *check = new QCheckBox(second);
        second->setControl(check);
        auto *detail = new QLabel(QStringLiteral("Status"), row);
        row->setDetail(detail);
        card.addRow(second);
        card.show();
        QCoreApplication::processEvents();

        QVERIFY(card.titleLabel()->font().bold());
        QCOMPARE(card.title(), QStringLiteral("Card"));
        // Controls of different widths end on the same column: the row's right
        // padding edge.
        QCOMPARE(control->mapTo(row, QPoint(control->width(), 0)).x(),
                 row->width() - settings::rowHorizontalPadding());
        QCOMPARE(check->mapTo(second, QPoint(check->width(), 0)).x(),
                 second->width() - settings::rowHorizontalPadding());
        QCOMPARE(second->titleLabel()->buddy(), check);
        QCOMPARE(detail->foregroundRole(), QPalette::PlaceholderText);
        QTest::mousePress(check,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          QPoint(check->width() / 2, check->height() / 2));
        QTest::mouseRelease(check,
                            Qt::LeftButton,
                            Qt::NoModifier,
                            QPoint(-1, check->height() / 2));
        QVERIFY(!check->isChecked());
        QTest::mouseRelease(second,
                            Qt::LeftButton,
                            Qt::NoModifier,
                            QPoint(settings::rowHorizontalPadding(), second->height() / 2));
        QVERIFY(!check->isChecked());
        QTest::mouseClick(second,
                          Qt::LeftButton,
                          Qt::NoModifier,
                          QPoint(settings::rowHorizontalPadding(), second->height() / 2));
        QVERIFY(check->isChecked());
        QVERIFY(check->hasFocus());
        // The control is vertically centred on the text beside it.
        const QRect controlInRow(control->mapTo(row, QPoint()), control->size());
        QWidget *textColumn = row->titleLabel()->parentWidget();
        const QRect textInRow(textColumn->mapTo(row, QPoint()), textColumn->size());
        QVERIFY(qAbs(controlInRow.center().y() - textInRow.center().y()) <= 1);
        // The subtitle wraps, and the row is tall enough for every line.
        QLabel *help = row->subtitleLabel();
        QVERIFY(help->wordWrap());
        QVERIFY(help->heightForWidth(help->width()) > help->fontMetrics().height());
        QVERIFY(help->height() >= help->heightForWidth(help->width()));
        QVERIFY(row->rect().contains(QRect(help->mapTo(row, QPoint()), help->size())));
        // One separator between two rows, drawn by the style as a line frame.
        const QList<QFrame *> separators = card.findChildren<QFrame *>(QStringLiteral("rowSeparator"));
        QCOMPARE(separators.size(), 1);
        QCOMPARE(separators.first()->frameShape(), QFrame::HLine);
        QVERIFY(card.body()->title().isEmpty());
        QVERIFY(!card.body()->isFlat());
        // A row is what a search matches and points at.
        QVERIFY(row->searchText().contains(QStringLiteral("Control")));
        QVERIFY(!row->isFlashing());
        row->flash();
        QVERIFY(row->isFlashing());
        QTest::qWait(700);
        row->flash();
        QTest::qWait(600);
        QVERIFY(row->isFlashing());
        QTRY_VERIFY_WITH_TIMEOUT(!row->isFlashing(), 2000);
        QCOMPARE(detail->foregroundRole(), QPalette::PlaceholderText);
    }

    void accountTextFieldsSpanTheCard()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setOpenAiAuthMode(QStringLiteral("cliproxy"));
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        ProviderCustomRows providerRows(settings, secrets);
        const std::unique_ptr<SchemaSettingsPage> page = schemaPage(
            QStringLiteral("accounts"), *platformComposition(), providers, providerRows.factory());
        page->load(settings.snapshot());
        page->resize(900, 900);
        page->show();
        QCoreApplication::processEvents();

        for (const QString &id : {QStringLiteral("cliproxyBaseUrl"),
                                  QStringLiteral("cliproxyApiKey")}) {
            auto *edit = page->findChild<QLineEdit *>(id);
            QVERIFY2(edit, qPrintable(id));
            QVERIFY2(edit->isVisibleTo(page.get()), qPrintable(id));
            auto *row = ancestorOf<settings::FormRow>(edit);
            QVERIFY(row);
            QCOMPARE(row->editor(), edit);
            QVERIFY(edit->width() > settings::controlMinimumWidth() * 2);
        }

        settings.setOpenAiAuthMode(QStringLiteral("settings"));
        page->load(settings.snapshot());
        QCoreApplication::processEvents();
        auto *apiKey = page->findChild<QLineEdit *>(QStringLiteral("openAiAuth"));
        QVERIFY(apiKey);
        QVERIFY(apiKey->isVisibleTo(page.get()));
        QCOMPARE(ancestorOf<settings::FormRow>(apiKey)->editor(), apiKey);
    }

    void settingsCardsFitANarrowPaneWithoutClipping()
    {
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        const std::unique_ptr<SchemaSettingsPage> page =
            schemaPage(QStringLiteral("general"), *platform, providers);
        // The long update toggle only shows when automatic downloads exist.
        page->setCapabilities({false, true});
        page->resize(500, 700);
        page->show();
        QCoreApplication::processEvents();

        QVERIFY(page->horizontalScrollBar()->maximum() == 0);
        auto *autoInstall = page->findChild<QCheckBox *>(QStringLiteral("autoInstallUpdates"));
        QVERIFY(autoInstall);
        QVERIFY(autoInstall->isVisibleTo(page.get()));
        auto *column = page->findChild<QWidget *>(QStringLiteral("cardColumn"));
        QVERIFY(column);
        QVERIFY(column->width() <= page->viewport()->width());
        for (settings::FormRow *row : shownRows(*page)) {
            QWidget *body = row->parentWidget();
            const QRect inBody(row->mapTo(body, QPoint(0, 0)), row->size());
            QVERIFY2(inBody.right() <= body->width(), qPrintable(row->title()));
            QVERIFY2(inBody.left() >= 0, qPrintable(row->title()));
        }
    }

    // At the window sizes people actually use: every subtitle fits its row
    // unclipped, and every control ends on the same right-hand column.
    void settingsPagesKeepHelpUnclippedAndControlsAligned()
    {
        SettingsStore settings;
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        const std::shared_ptr<const PlatformComposition> platform = platformComposition();
        // 900x900 is the screenshot size; 540x520 is what a 760x520 window
        // leaves beside its sidebar.
        for (const QSize &size : {QSize(900, 900), QSize(540, 520)}) {
            // The custom-row helpers hold on to the widgets they made, so each
            // set of pages gets its own.
            OutputCustomRows outputRows(settings);
            ProviderCustomRows providerRows(settings, secrets);
            BindingRows bindingRows;
            const QList<std::pair<QString, SchemaCustomRowFactory>> pages{
                {QStringLiteral("general"), {}},
                {QStringLiteral("audio"), {}},
                {QStringLiteral("output"), outputRows.factory()},
                {QStringLiteral("accounts"), providerRows.factory()},
                {QStringLiteral("refinement"), {}},
                {QStringLiteral("vocabulary"), bindingRows.factory()},
            };
            for (const auto &[id, factory] : pages) {
                const std::unique_ptr<SchemaSettingsPage> page =
                    schemaPage(id, *platform, providers, factory);
                page->setCapabilities({true, true});
                page->load(settings.snapshot());
                page->resize(size);
                page->show();
                QCoreApplication::processEvents();

                QVERIFY2(page->horizontalScrollBar()->maximum() == 0, qPrintable(id));
                QWidget *content = page->widget();
                std::optional<int> column;
                for (settings::FormRow *row : shownRows(*page)) {
                    const QString where = id + QStringLiteral("/") + row->title();
                    QLabel *help = row->subtitleLabel();
                    if (help->isVisibleTo(page.get())) {
                        QVERIFY2(help->height() >= help->heightForWidth(help->width()), qPrintable(where));
                        QVERIFY2(row->rect().contains(QRect(help->mapTo(row, QPoint()), help->size())),
                                 qPrintable(where));
                    }
                    QWidget *control = row->control();
                    if (control && control->isVisibleTo(page.get())) {
                        const int right = control->mapTo(content, QPoint(control->width(), 0)).x();
                        QCOMPARE(right, controlRightEdge(row, content));
                        if (!column) {
                            column = right;
                        }
                        QVERIFY2(right == *column, qPrintable(where));
                    }
                    if (QWidget *editor = row->editor()) {
                        // A spanning editor starts where the title does.
                        const int editorLeft = editor->mapTo(row, QPoint()).x();
                        QCOMPARE(editorLeft, settings::rowHorizontalPadding());
                        if (!row->title().isEmpty()) {
                            QCOMPARE(row->titleLabel()->mapTo(row, QPoint()).x(), editorLeft);
                        }
                    }
                }
            }
        }
    }

    void settingsRowsGrowForWrappedDescriptions()
    {
        QWidget surface;
        surface.resize(520, 240);
        auto *layout = new QVBoxLayout(&surface);
        auto *row = new settings::FormRow(
            QStringLiteral("Output"),
            QStringLiteral("How Speecher delivers final text after dictation has completed, "
                           "including the fallback used when the preferred delivery is missing"),
            &surface);
        auto *control = new QPushButton(QStringLiteral("A deliberately wide control"), row);
        control->setFixedWidth(250);
        row->setControl(control);
        layout->addWidget(row);
        layout->addStretch();

        surface.show();
        QCoreApplication::processEvents();
        QLabel *description = row->subtitleLabel();
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
