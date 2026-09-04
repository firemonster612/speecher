#include "common/test_suites.h"

#include "core/BindingProcessor.h"
#include "core/VocabularyLimit.h"
#include "core/settings/SettingsSchema.h"

#include <algorithm>

using namespace speecher;

namespace {

SchemaContext fakeContext()
{
    return {
        {{QStringLiteral("claude"), QStringLiteral("Claude Voice")}},
        {{QStringLiteral("openai"), QStringLiteral("OpenAI"), true}},
        [] {
            return QList<RowOption>{{QStringLiteral("mic-1"), QStringLiteral("Desk microphone")}};
        },
    };
}

const SettingsRow &rowById(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return row;
            }
        }
    }
    qFatal("no row %s on page %s", qPrintable(id), qPrintable(page.id));
}

bool hasRow(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        if (std::any_of(section.rows.begin(), section.rows.end(), [&id](const SettingsRow &row) {
                return row.id == id;
            })) {
            return true;
        }
    }
    return false;
}

} // namespace

class SettingsSchemaTests : public QObject {
    Q_OBJECT

private slots:
    void baseVersionsCompareNumericallyWithoutNightlySuffixes()
    {
        QCOMPARE(compareBaseVersions(QStringLiteral("0.2.0-nightly.20260901+gabc1234"),
                                     QStringLiteral("0.2.0")),
                 0);
        QVERIFY(compareBaseVersions(QStringLiteral("0.10.0"), QStringLiteral("0.2.0")) > 0);
        QVERIFY(compareBaseVersions(QStringLiteral("0.2.0"), QStringLiteral("0.2.1")) < 0);
        QCOMPARE(compareBaseVersions(QStringLiteral("0.2.9+g1"), QStringLiteral("0.2.9")),
                 0);
        QVERIFY(compareBaseVersions(QString(), QStringLiteral("0.1.0")) < 0);
    }

    void whatsNewPageSelectsLiveRowsInTheVersionRange()
    {
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.0.0");
        context.currentVersion = QStringLiteral("0.1.0-nightly.20260901+gabc1234");
        SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow &channel = rowById(schema.page(QStringLiteral("whatsNew")),
                                             QStringLiteral("updateChannel"));

        QCOMPARE(channel.sinceVersion, QStringLiteral("0.1.0"));
        AppSettings settings;
        channel.apply(settings, QStringLiteral("nightly"));
        QCOMPARE(settings.updates.channel, UpdateChannel::Nightly);

        context.lastSeenVersion = QStringLiteral("0.1.0");
        schema = buildSettingsSchema(context);
        QVERIFY(!hasRow(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("updateChannel")));

        context.lastSeenVersion.clear();
        context.currentVersion = QStringLiteral("0.1.1");
        schema = buildSettingsSchema(context);
        QVERIFY(!hasRow(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("updateChannel")));
        QVERIFY(rowById(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("whatsNewNotes"))
                    .value(AppSettings{})
                    .toString()
                    .contains(QStringLiteral("Speecher 0.1.1")));

        context.lastSeenVersion = QStringLiteral("0.1.0");
        context.currentVersion = QStringLiteral("0.1.1");
        schema = buildSettingsSchema(context);
        const SettingsPage &whatsNew = schema.page(QStringLiteral("whatsNew"));
        QVERIFY(!hasRow(whatsNew, QStringLiteral("checkForUpdates")));
        QVERIFY(!hasRow(whatsNew, QStringLiteral("currentVersion")));
        QVERIFY(!hasRow(whatsNew, QStringLiteral("whatsNew")));
    }

    void whatsNewNotesSelectEveryReleaseInTheVersionRange()
    {
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.0.0");
        context.currentVersion = QStringLiteral("0.1.1");
        const QString notes = rowById(buildSettingsSchema(context).page(
                                          QStringLiteral("whatsNew")),
                                      QStringLiteral("whatsNewNotes"))
                                  .value(AppSettings{})
                                  .toString();

        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.1.0")));
        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.1.1")));
        QVERIFY(notes.indexOf(QStringLiteral("# Speecher 0.1.1"))
                < notes.indexOf(QStringLiteral("# Speecher 0.1.0")));
    }

    void nightlyNotesLinkToTheComparedCommits()
    {
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.1.0-nightly.20260831+gabc1234");
        context.currentVersion = QStringLiteral("0.2.0-nightly.20260901+gdef5678");
        const QString notes = rowById(buildSettingsSchema(context).page(
                                          QStringLiteral("whatsNew")),
                                      QStringLiteral("whatsNewNotes"))
                                  .value(AppSettings{})
                                  .toString();

        QVERIFY(notes.contains(QStringLiteral(
            "https://github.com/firemonster612/speecher/compare/abc1234...def5678")));
    }

    void rowsRoundTripAValueThroughAppSettings()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &previewWords = rowById(schema.page(QStringLiteral("general")),
                                                  QStringLiteral("previewWords"));
        const SettingsRow &captureMode = rowById(schema.page(QStringLiteral("audio")),
                                                 QStringLiteral("captureMode"));
        const SettingsRow &profiles = rowById(schema.page(QStringLiteral("refinement")),
                                              QStringLiteral("writingProfileBehavior"));

        AppSettings settings;
        previewWords.apply(settings, 21);
        captureMode.apply(settings, QStringLiteral("warm"));
        profiles.apply(settings,
                       QVariant::fromValue(QList<WritingProfileSettings>{
                           {WritingProfile::Email, QStringLiteral("strong_polish"), QStringLiteral("formal")}}));

        QCOMPARE(settings.ui.previewWords, 21);
        QCOMPARE(settings.audio.mode, QStringLiteral("warm"));
        QCOMPARE(previewWords.value(settings).toInt(), 21);
        QCOMPARE(captureMode.value(settings).toString(), QStringLiteral("warm"));
        QCOMPARE(profiles.value(settings).value<QList<WritingProfileSettings>>().size(), 1);
        QVERIFY(profiles.value(settings) != profiles.value(AppSettings{}));
    }

    void launchAtLoginAppearsOnDesktopPlatforms()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
        QVERIFY(hasRow(general, QStringLiteral("launchAtLogin")));
        const SettingsRow &row = rowById(general, QStringLiteral("launchAtLogin"));
        QCOMPARE(row.kind, RowKind::Toggle);
        QCOMPARE(row.label, QStringLiteral("Start Speecher at login"));
        QCOMPARE(row.help,
                 QStringLiteral("Dictation only works while Speecher is running"));
#else
        QVERIFY(!hasRow(general, QStringLiteral("launchAtLogin")));
#endif
    }

    void pagesAreTheSevenTheSidebarShows()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        QStringList ids;
        for (const SettingsPage &page : schema.pages) {
            ids.append(page.id);
        }
        // Dictation is not a schema page; What's New is reached from General.
        QCOMPARE(ids,
                 QStringList({QStringLiteral("general"),
                              QStringLiteral("audio"),
                              QStringLiteral("output"),
                              QStringLiteral("accounts"),
                              QStringLiteral("refinement"),
                              QStringLiteral("vocabulary"),
                              QStringLiteral("whatsNew")}));
    }

    void everyCardHasATitleAndHelpIsOneLine()
    {
        SchemaContext context = fakeContext();
        context.currentVersion = QStringLiteral("0.1.4");
        const SettingsSchema schema = buildSettingsSchema(context);
        const auto oneLine = [](const QString &text, const QString &where) {
            // One sentence, no full stop: a subtitle, not a paragraph.
            QVERIFY2(!text.contains(QStringLiteral(". ")), qPrintable(where + QStringLiteral(": ") + text));
            QVERIFY2(!text.endsWith(QLatin1Char('.')), qPrintable(where + QStringLiteral(": ") + text));
        };
        for (const SettingsPage &page : schema.pages) {
            QVERIFY(!page.sections.isEmpty());
            for (const SettingsSection &section : page.sections) {
                QVERIFY2(!section.title.isEmpty(), qPrintable(page.id));
                oneLine(section.help, section.title);
                for (const SettingsRow &row : section.rows) {
                    oneLine(row.help, row.id);
                    oneLine(row.disabledHelp, row.id);
                }
            }
        }
    }

    void generalCardsFollowTheSettledOrder()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        QStringList titles;
        for (const SettingsSection &section : schema.page(QStringLiteral("general")).sections) {
            titles.append(section.title);
        }
#ifdef Q_OS_MACOS
        QCOMPARE(titles,
                 QStringList({QStringLiteral("Appearance"), QStringLiteral("Dictation"),
                              QStringLiteral("Startup"), QStringLiteral("Updates"),
                              QStringLiteral("Setup")}));
#else
        // The desktop decides the colour scheme, while start-at-login is an
        // XDG autostart entry on Linux.
        QCOMPARE(titles,
                 QStringList({QStringLiteral("Dictation"), QStringLiteral("Startup"),
                              QStringLiteral("Updates"),
                              QStringLiteral("Setup")}));
        QVERIFY(!hasRow(schema.page(QStringLiteral("general")), QStringLiteral("themeControl")));
#endif
        const SettingsSection &updates = schema.page(QStringLiteral("general")).sections.at(
            titles.indexOf(QStringLiteral("Updates")));
        QStringList ids;
        for (const SettingsRow &row : updates.rows) {
            ids.append(row.id);
        }
        QCOMPARE(ids,
                 QStringList({QStringLiteral("updateChannel"), QStringLiteral("autoCheckUpdates"),
                              QStringLiteral("autoInstallUpdates"),
                              QStringLiteral("checkForUpdates"), QStringLiteral("whatsNew")}));
    }

    void checkForUpdatesNamesTheRunningVersion()
    {
        SchemaContext context = fakeContext();
        context.currentVersion = QStringLiteral("0.1.4");
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsPage &general = schema.page(QStringLiteral("general"));
        QCOMPARE(rowById(general, QStringLiteral("checkForUpdates")).help,
                 QStringLiteral("Speecher 0.1.4"));
        QVERIFY(!hasRow(general, QStringLiteral("currentVersion")));
    }

    void outputAudioAndVocabularyCardsFollowTheSettledOrder()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const auto titles = [&schema](const QString &pageId) {
            QStringList titles;
            for (const SettingsSection &section : schema.page(pageId).sections) {
                titles.append(section.title);
            }
            return titles;
        };
        QCOMPARE(titles(QStringLiteral("audio")),
                 QStringList({QStringLiteral("Transcription"), QStringLiteral("Microphone"),
                              QStringLiteral("Silence detection"), QStringLiteral("Timing")}));
        QCOMPARE(titles(QStringLiteral("output")),
                 QStringList({QStringLiteral("How text is inserted"), QStringLiteral("Per-app rules"),
                              QStringLiteral("Feedback")}));
        QCOMPARE(titles(QStringLiteral("accounts")),
                 QStringList({QStringLiteral("ChatGPT / Codex"), QStringLiteral("Claude"),
                              QStringLiteral("CLI Proxy API server")}));
        QCOMPARE(titles(QStringLiteral("refinement")),
                 QStringList({QStringLiteral("Refinement"), QStringLiteral("Writing profiles"),
                              QStringLiteral("OpenAI"), QStringLiteral("Claude")}));
        QCOMPARE(titles(QStringLiteral("vocabulary")),
                 QStringList({QStringLiteral("Vocabulary"), QStringLiteral("Learned corrections"),
                              QStringLiteral("Replacements and snippets")}));
        // The Applications page is gone; its rules are a row on Output, gated
        // with the paste rules they share a target with.
        const SettingsPage &output = schema.page(QStringLiteral("output"));
        QVERIFY(hasRow(output, QStringLiteral("appRecognitionRules")));
        QCOMPARE(rowById(output, QStringLiteral("appRecognitionRules")).groupId,
                 rowById(output, QStringLiteral("applicationPasteRules")).groupId);
    }

    void collectionSizesAreRowCountsRatherThanPixels()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        for (const QString &id : {QStringLiteral("appRecognitionRules"),
                                  QStringLiteral("applicationPasteRules"),
                                  QStringLiteral("vocabularyEntries"),
                                  QStringLiteral("learnedCorrections"),
                                  QStringLiteral("bindingRules")}) {
            const QString page = id == QStringLiteral("appRecognitionRules")
                    || id == QStringLiteral("applicationPasteRules")
                ? QStringLiteral("output")
                : QStringLiteral("vocabulary");
            QVERIFY2(rowById(schema.page(page), id).collection.minimumVisibleRows > 0,
                     qPrintable(id));
        }
    }

    void audioTimingControlsSitUnderTimingInPlainWords()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &audio = schema.page(QStringLiteral("audio"));
        const SettingsSection &timing = audio.sections.last();
        QCOMPARE(timing.title, QStringLiteral("Timing"));
        QStringList ids;
        for (const SettingsRow &row : timing.rows) {
            ids.append(row.id);
        }
        QCOMPARE(ids,
                 QStringList({QStringLiteral("preRollMs"),
                              QStringLiteral("postRollMs"),
                              QStringLiteral("readinessTimeoutMs")}));
        for (const SettingsSection &section : audio.sections) {
            for (const SettingsRow &row : section.rows) {
                for (const QString &jargon : {QStringLiteral("RMS"), QStringLiteral("VAD"),
                                              QStringLiteral("roll"), QStringLiteral("Warm"),
                                              QStringLiteral("Readiness")}) {
                    QVERIFY2(!row.label.contains(jargon) && !row.help.contains(jargon),
                             qPrintable(row.id + QStringLiteral(": ") + jargon));
                }
            }
        }
    }

    void linuxGeneralOffersRemoval()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
#ifdef Q_OS_LINUX
        const SettingsRow &row = rowById(general, QStringLiteral("removeSpeecher"));
        QCOMPARE(row.kind, RowKind::Action);
        QCOMPARE(row.label, QStringLiteral("Remove Speecher from this computer"));
        QCOMPARE(row.actionLabel, QStringLiteral("Remove…"));
        QVERIFY(row.help.contains(QStringLiteral("app menu entry")));
#else
        QVERIFY(!hasRow(general, QStringLiteral("removeSpeecher")));
#endif
    }

    void schemaCopyNamesNoImplementation()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const QStringList internals{QStringLiteral("AT-SPI"), QStringLiteral("ydotool"),
                                    QStringLiteral("wl-copy"), QStringLiteral("OAuth"),
                                    QStringLiteral("QtKeychain"), QStringLiteral("OPENAI_API_KEY"),
                                    QStringLiteral("Deepgram"), QStringLiteral("RMS"),
                                    QStringLiteral("VAD")};
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    for (const QString &word : internals) {
                        const QString where = row.id + QStringLiteral(" mentions ") + word;
                        QVERIFY2(!row.label.contains(word), qPrintable(where));
                        QVERIFY2(!row.help.contains(word), qPrintable(where));
                        QVERIFY2(!row.disabledHelp.contains(word), qPrintable(where));
                        QVERIFY2(!row.actionLabel.contains(word), qPrintable(where));
                    }
                }
                for (const QString &word : internals) {
                    QVERIFY2(!section.help.contains(word), qPrintable(section.title + word));
                }
            }
        }
    }

    void generalHasNoClipboardStatusRow()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        QVERIFY(!hasRow(schema.page(QStringLiteral("general")),
                        QStringLiteral("clipboardOutputStatus")));
    }

    void targetContextNeedsAccessibility()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("refinement")),
                                         QStringLiteral("targetContextControl"));

        QVERIFY(!row.enabled(AppSettings{}, Capabilities{false}));
        QVERIFY(row.enabled(AppSettings{}, Capabilities{true}));
        QVERIFY(!row.disabledHelp.isEmpty());

        // Every gate names the feature the same way, without the service name.
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &candidate : section.rows) {
                    QVERIFY2(!candidate.disabledHelp.contains(QStringLiteral("AT-SPI")),
                             qPrintable(candidate.id));
                    QVERIFY2(!candidate.help.contains(QStringLiteral("AT-SPI")),
                             qPrintable(candidate.id));
                }
            }
        }
    }

#ifdef Q_OS_MACOS
    void themeRowExplainsItselfWhenTheDesktopIgnoresIt()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("themeControl"));
        Capabilities honoured;
        QVERIFY(row.enabled(AppSettings{}, honoured));
        Capabilities ignored;
        ignored.colorSchemeOverride = false;
        QVERIFY(!row.enabled(AppSettings{}, ignored));
        QVERIFY(row.disabledHelp.contains(QStringLiteral("desktop")));
    }
#endif

    void automaticInstallOnlyAppearsWhenSupported()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("autoInstallUpdates"));

        QVERIFY(!row.visible(AppSettings{}, Capabilities{false, false}));
        QVERIFY(row.visible(AppSettings{}, Capabilities{false, true}));
    }

    void theOnlySlowRowsAreTheDeviceListAndTheKeyring()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        QStringList expensive;
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    if (row.expensive) {
                        expensive.append(row.id);
                    }
                }
            }
        }
        QCOMPARE(expensive,
                 QStringList({QStringLiteral("audioDevice"), QStringLiteral("openAiAuth")}));
    }

    void screenshotContextFollowsWhatTheProviderCanDo()
    {
        SchemaContext context = fakeContext();
        context.refinementProviders.append(
            {QStringLiteral("local"), QStringLiteral("Local"), false});
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow &row = rowById(schema.page(QStringLiteral("refinement")),
                                         QStringLiteral("includeScreenshotContext"));

        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("openai");
        QVERIFY(row.enabled(settings, Capabilities{}));
        settings.refinement.providerId = QStringLiteral("local");
        QVERIFY(!row.enabled(settings, Capabilities{}));
    }

    void everyProviderIsTheSameFragmentOnBothPages()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        // Models on Refinement: one card per service, the same shape each.
        const SettingsPage &refinement = schema.page(QStringLiteral("refinement"));
        for (const SettingsSection &section : refinement.sections.sliced(2)) {
            const SettingsRow &model = section.rows.first();
            QCOMPARE(model.label, QStringLiteral("Model"));
            QCOMPARE(model.kind, RowKind::Text);
            QVERIFY(!model.suggestions(AppSettings{}).isEmpty());
            QVERIFY(std::any_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Choice;
            }));
            QVERIFY(std::any_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Toggle;
            }));
        }
        // Sign-in on Accounts: one card per service, then the server card.
        const SettingsPage &accounts = schema.page(QStringLiteral("accounts"));
        QCOMPARE(accounts.sections.size(), 3);
        for (const SettingsSection &section : accounts.sections.sliced(0, 2)) {
            QVERIFY(std::all_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Custom;
            }));
            QCOMPARE(section.rows.first().label, QStringLiteral("Account"));
            QVERIFY(hasRow(SettingsPage{{}, {}, {}, {}, {section}}, QStringLiteral("openAiAuthMode"))
                    || hasRow(SettingsPage{{}, {}, {}, {}, {section}}, QStringLiteral("anthropicAuthMode")));
        }

        AppSettings settings;
        rowById(refinement, QStringLiteral("openAiModel")).apply(settings, QStringLiteral("gpt-5.4"));
        rowById(refinement, QStringLiteral("anthropicEffort")).apply(settings, QStringLiteral("max"));
        QCOMPARE(settings.refinement.openAiModel, QStringLiteral("gpt-5.4"));
        QCOMPARE(settings.refinement.anthropicEffort, QStringLiteral("max"));

        QCOMPARE(rowById(refinement, QStringLiteral("openAiFastMode")).value(settings).toBool(), true);
        QCOMPARE(rowById(refinement, QStringLiteral("anthropicFastMode")).value(settings).toBool(), true);
        rowById(refinement, QStringLiteral("openAiFastMode")).apply(settings, false);
        rowById(refinement, QStringLiteral("anthropicFastMode")).apply(settings, false);
        QCOMPARE(settings.refinement.openAiFastMode, false);
        QCOMPARE(settings.refinement.anthropicFastMode, false);

        const SettingsSection &server = accounts.sections.last();
        QCOMPARE(server.title, QStringLiteral("CLI Proxy API server"));
        QCOMPARE(server.rows.size(), 2);
        QCOMPARE(server.rows.at(0).id, QStringLiteral("cliproxyBaseUrl"));
        QCOMPARE(server.rows.at(1).id, QStringLiteral("cliproxyApiKey"));
    }

    void accountRowsSpeakOfSignInNotCredentialSources()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &page = schema.page(QStringLiteral("accounts"));
        for (const SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                QVERIFY2(!row.label.contains(QStringLiteral("auth"), Qt::CaseInsensitive),
                         qPrintable(row.label));
                QVERIFY2(!row.help.contains(QStringLiteral("OPENAI_API_KEY")), qPrintable(row.id));
                QVERIFY2(!row.help.contains(QStringLiteral("credential"), Qt::CaseInsensitive),
                         qPrintable(row.id));
            }
            // The card's one line is one sentence, not a five-source fallback chain.
            QVERIFY2(section.help.count(QStringLiteral(". ")) == 0, qPrintable(section.help));
        }
        // The status leads each card, then the way the sign-in is chosen.
        QCOMPARE(page.sections.first().rows.first().id, QStringLiteral("openAiAuth"));
        QCOMPARE(page.sections.at(1).rows.first().id, QStringLiteral("anthropicAuth"));
        QCOMPARE(rowById(page, QStringLiteral("openAiAuth")).label, QStringLiteral("Account"));
        QCOMPARE(rowById(page, QStringLiteral("anthropicAuth")).label, QStringLiteral("Account"));
        QCOMPARE(rowById(page, QStringLiteral("openAiAuthMode")).label, QStringLiteral("Sign-in"));
        QCOMPARE(rowById(page, QStringLiteral("anthropicAuthMode")).label, QStringLiteral("Sign-in"));
    }

    void aModelThatReadsTranscriptsAsInstructionsSaysSo()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &caution = rowById(schema.page(QStringLiteral("refinement")),
                                             QStringLiteral("anthropicModelCaution"));
        AppSettings settings;
        QVERIFY(!caution.visible(settings, Capabilities{}));
        settings.refinement.anthropicModel = QStringLiteral("claude-haiku-4-5-20251001");
        QVERIFY(caution.visible(settings, Capabilities{}));
        QVERIFY(caution.value(settings).toString().contains(QStringLiteral("instructions")));
    }

    void recognitionRecordsRoundTripAndRetireLegacyOverrides()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("output")),
                                         QStringLiteral("appRecognitionRules"));
        const int locked = row.collection.lockedRecordCount();
        QCOMPARE(locked, int(builtInAppRecognitionRules().size()));

        AppSettings settings;
        settings.refinement.writingProfileOverrides = {
            {QStringLiteral("org.legacy.chat"), WritingProfile::Personal, true},
        };
        const QList<QVariantMap> records = row.collection.records(settings);
        QCOMPARE(records.size(), locked + 1);
        QCOMPARE(records.first().value(QStringLiteral("source")).toString(),
                 QStringLiteral("Built-in"));
        QCOMPARE(records.last().value(QStringLiteral("match")).toString(),
                 QStringLiteral("org.legacy.chat"));
        QCOMPARE(records.last().value(QStringLiteral("profile")).toString(),
                 QStringLiteral("personal"));

        // The locked records are shown, never applied.
        row.collection.apply(settings, records.mid(locked));
        QCOMPARE(settings.appRecognitionRules.size(), 1);
        QCOMPARE(settings.appRecognitionRules.first().match, QStringLiteral("org.legacy.chat"));
        QCOMPARE(settings.appRecognitionRules.first().writingProfile, WritingProfile::Personal);
        QVERIFY(settings.refinement.writingProfileOverrides.isEmpty());
        QCOMPARE(row.collection.records(settings).size(), locked + 1);
    }

    void applicationPasteRulesRoundTripAndRefuseDuplicates()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("output")),
                                         QStringLiteral("applicationPasteRules"));
        const QList<QVariantMap> records{
            {{QStringLiteral("enabled"), true},
             {QStringLiteral("application"), QStringLiteral("org.example.App")},
             {QStringLiteral("method"), QStringLiteral("clipboard_only")}},
            {{QStringLiteral("enabled"), true},
             {QStringLiteral("application"), QStringLiteral("ORG.EXAMPLE.APP")},
             {QStringLiteral("method"), QStringLiteral("standard_paste")}},
        };
        QCOMPARE(row.collection.validate(records),
                 QStringList{QStringLiteral("Each application ID can have only one paste rule.")});
        QVERIFY(row.collection.validate(records.mid(0, 1)).isEmpty());

        AppSettings settings;
        row.collection.apply(settings, records.mid(0, 1));
        QCOMPARE(row.collection.records(settings), records.mid(0, 1));
        // The category and global rules it does not own are still there.
        QCOMPARE(settings.output.pasteRules.size(), defaultPasteRules().size() + 1);
    }

    void aPasteRuleForAnUnmanagedCategorySurvivesTheOnesThisBuildOffers()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &terminals = rowById(schema.page(QStringLiteral("output")),
                                               QStringLiteral("categoryPasteRule_terminal"));
        AppSettings settings;
        settings.output.pasteRules = {
            {PasteRuleScope::Category, QStringLiteral("unknown"), PasteMethod::ClipboardOnly, true},
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };

        QCOMPARE(terminals.value(settings).toString(), QStringLiteral("inherit"));
        terminals.apply(settings, QStringLiteral("terminal_paste"));
        QCOMPARE(terminals.value(settings).toString(), QStringLiteral("terminal_paste"));
        terminals.apply(settings, QStringLiteral("inherit"));
        QCOMPARE(settings.output.pasteRules.size(), 2);
        QCOMPARE(settings.output.pasteRules.first().match, QStringLiteral("unknown"));
    }

    void vocabularyIsNormalisedWhenItIsApplied()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("vocabulary")),
                                         QStringLiteral("vocabularyEntries"));
        const QList<QVariantMap> records{
            {{QStringLiteral("starred"), false},
             {QStringLiteral("term"), QStringLiteral("Speecher")},
             {QStringLiteral("source"), QStringLiteral("manual")},
             {QStringLiteral("uses"), 3},
             {QStringLiteral("lastUsedMs"), qint64(1750000000000)}},
            {{QStringLiteral("starred"), true},
             {QStringLiteral("term"), QStringLiteral("  speecher  ")},
             {QStringLiteral("source"), QStringLiteral("imported")},
             {QStringLiteral("uses"), 0},
             {QStringLiteral("lastUsedMs"), qint64(0)}},
            {{QStringLiteral("term"), QStringLiteral("   ")}},
        };

        AppSettings settings;
        row.collection.apply(settings, records);
        QCOMPARE(settings.vocabulary.size(), 1);
        QCOMPARE(settings.vocabulary.first().term, QStringLiteral("Speecher"));
        QVERIFY(settings.vocabulary.first().starred);
        QCOMPARE(settings.vocabulary.first().frequency, 3);

        // The count a reader sees comes from the same summary the row shows.
        const SettingsRow &limit = rowById(schema.page(QStringLiteral("vocabulary")),
                                           QStringLiteral("vocabularyLimit"));
        QCOMPARE(limit.value(settings).toString(),
                 VocabularyLimit::summary({QStringLiteral("Speecher")}));
    }

    void aCorrectionKeepsTheFieldsNoColumnShows()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("vocabulary")),
                                         QStringLiteral("learnedCorrections"));
        AppSettings settings;
        settings.learnedCorrections = {{QStringLiteral("c-1"),
                                        QStringLiteral("speecher"),
                                        QStringLiteral("Speecher"),
                                        QStringLiteral("org.kde.konsole"),
                                        1750000000000,
                                        0.92,
                                        true,
                                        3,
                                        1750000900000}};

        QList<QVariantMap> records = row.collection.records(settings);
        QCOMPARE(records.size(), 1);
        records[0][QStringLiteral("corrected")] = QStringLiteral("Speecher!");
        AppSettings edited;
        row.collection.apply(edited, records);

        LearnedCorrection expected = settings.learnedCorrections.first();
        expected.corrected = QStringLiteral("Speecher!");
        QCOMPARE(edited.learnedCorrections, QList<LearnedCorrection>{expected});
    }

    void aCorrectionColumnCanSayWhatItsRecordKnows()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("vocabulary")),
                                         QStringLiteral("learnedCorrections"));
        const CollectionColumn &application = row.collection.columns.last();
        QVERIFY(application.recordTooltip);
        QCOMPARE(application.recordTooltip({{QStringLiteral("confidence"), 0.92}}),
                 QStringLiteral("Learned automatically · confidence 92%"));
    }

    void replacementValidationSpeaksForItself()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("vocabulary")),
                                         QStringLiteral("bindingRules"));
        const QList<QVariantMap> records{
            {{QStringLiteral("phrase"), QStringLiteral("my,email")},
             {QStringLiteral("replacement"), QStringLiteral("one")}},
            {{QStringLiteral("phrase"), QStringLiteral("MY email")},
             {QStringLiteral("replacement"), QStringLiteral("two")}},
        };
        QCOMPARE(row.collection.validate(records),
                 BindingProcessor::validateRules({{QStringLiteral("my,email"), QStringLiteral("one")},
                                                  {QStringLiteral("MY email"), QStringLiteral("two")}})
                     .messages());
        QVERIFY(!row.collection.validate(records).isEmpty());

        AppSettings settings;
        row.collection.apply(settings, records.mid(0, 1));
        QCOMPARE(settings.bindings.size(), 1);
        QCOMPARE(row.collection.records(settings), records.mid(0, 1));
    }

    void snippetsAndVocabularyCanComeFromAFile()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const CollectionImport &csv =
            rowById(schema.page(QStringLiteral("vocabulary")), QStringLiteral("vocabularyEntries"))
                .collection.supportsImport;
        QString error;
        const QList<QVariantMap> terms = csv.parse("term\nDeepgram\n", &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(terms.size(), 1);
        QCOMPARE(terms.first().value(QStringLiteral("term")).toString(),
                 QStringLiteral("Deepgram"));

        const CollectionImport &json =
            rowById(schema.page(QStringLiteral("vocabulary")), QStringLiteral("bindingRules"))
                .collection.supportsImport;
        const QList<QVariantMap> snippets =
            json.parse(R"([{"phrase": "sign off", "replacement": "Thanks"}])", &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(snippets.size(), 1);
        QCOMPARE(snippets.first().value(QStringLiteral("replacement")).toString(),
                 QStringLiteral("Thanks"));
    }

    void restoreClipboardIsDescribedOnceForEverySurface()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("output")),
                                         QStringLiteral("restoreClipboardAfterTyping"));
        QCOMPARE(row.help, restoreClipboardDescription());
        QVERIFY(row.tooltip.isEmpty());
        QCOMPARE(restoreClipboardDescription(),
                 QStringLiteral("Restore the previous clipboard after Speecher confirms the "
                                "paste, or after a short delay when it cannot"));
    }

    void aSavedMicrophoneSurvivesGoingMissing()
    {
        const QList<RowOption> present{{QStringLiteral("mic-1"), QStringLiteral("Desk microphone")}};

        const QList<RowOption> withMissing =
            audioDeviceOptions(present, QStringLiteral("mic-gone"));
        QCOMPARE(withMissing.size(), 3);
        QCOMPARE(withMissing.last().id, QStringLiteral("mic-gone"));
        QVERIFY(!withMissing.last().enabled);

        const QList<RowOption> withPresent = audioDeviceOptions(present, QStringLiteral("mic-1"));
        QCOMPARE(withPresent.size(), 2);
        QCOMPARE(withPresent.first().id, QString());
    }
};

int runSettingsSchemaTests(int argc, char **argv)
{
    SettingsSchemaTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_settings_schema.moc"
