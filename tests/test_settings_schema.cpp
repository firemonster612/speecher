#include "common/test_suites.h"

#include "core/BindingProcessor.h"
#include "core/VocabularyLimit.h"
#include "core/settings/SettingsSchema.h"
#include "ui/settings/SettingsPageSet.h"

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
        QStringLiteral("Fake clipboard path"),
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
        context.lastSeenVersion = QStringLiteral("0.1.0");
        context.currentVersion = QStringLiteral("0.2.0-nightly.20260901+gabc1234");
        SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow &channel = rowById(schema.page(QStringLiteral("whatsNew")),
                                             QStringLiteral("updateChannel"));

        QCOMPARE(channel.sinceVersion, QStringLiteral("0.2.0"));
        AppSettings settings;
        channel.apply(settings, QStringLiteral("nightly"));
        QCOMPARE(settings.updates.channel, UpdateChannel::Nightly);

        context.lastSeenVersion = QStringLiteral("0.2.0");
        schema = buildSettingsSchema(context);
        QVERIFY(!hasRow(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("updateChannel")));

        context.lastSeenVersion.clear();
        schema = buildSettingsSchema(context);
        QVERIFY(!hasRow(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("updateChannel")));
        QVERIFY(rowById(schema.page(QStringLiteral("whatsNew")),
                        QStringLiteral("whatsNewNotes"))
                    .value(AppSettings{})
                    .toString()
                    .contains(QStringLiteral("Speecher 0.2.0")));

        context.lastSeenVersion = QStringLiteral("0.1.0");
        context.currentVersion = QStringLiteral("0.2.0");
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
        context.currentVersion = QStringLiteral("0.2.0");
        const QString notes = rowById(buildSettingsSchema(context).page(
                                          QStringLiteral("whatsNew")),
                                      QStringLiteral("whatsNewNotes"))
                                  .value(AppSettings{})
                                  .toString();

        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.1.0")));
        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.2.0")));
        QVERIFY(notes.indexOf(QStringLiteral("# Speecher 0.2.0"))
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

    void launchAtLoginOnlyAppearsOnMacOS()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
#ifdef Q_OS_MACOS
        QVERIFY(hasRow(general, QStringLiteral("launchAtLogin")));
        const SettingsRow &row = rowById(general, QStringLiteral("launchAtLogin"));
        QCOMPARE(row.kind, RowKind::Toggle);
        QCOMPARE(row.label, QStringLiteral("Start Speecher at login"));
        QCOMPARE(row.help,
                 QStringLiteral("Dictation only works while Speecher is running."));
#else
        QVERIFY(!hasRow(general, QStringLiteral("launchAtLogin")));
#endif
    }

    void targetContextNeedsAccessibility()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("refinement")),
                                         QStringLiteral("targetContextControl"));

        QVERIFY(!row.enabled(AppSettings{}, Capabilities{false}));
        QVERIFY(row.enabled(AppSettings{}, Capabilities{true}));
        QVERIFY(!row.disabledHelp.isEmpty());
    }

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

    void everyProviderAccountIsTheSameFragment()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &page = schema.page(QStringLiteral("providers"));
        QCOMPARE(page.sections.size(), 3);
        for (const SettingsSection &section : page.sections.sliced(0, 2)) {
            QVERIFY(section.title.endsWith(QStringLiteral("account")));
            const SettingsRow &model = section.rows.first();
            QCOMPARE(model.kind, RowKind::Text);
            QVERIFY(!model.suggestions(AppSettings{}).isEmpty());
            QVERIFY(std::any_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Choice;
            }));
            QVERIFY(std::any_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Custom;
            }));
            QVERIFY(std::any_of(section.rows.begin(), section.rows.end(), [](const SettingsRow &row) {
                return row.kind == RowKind::Toggle;
            }));
        }

        AppSettings settings;
        rowById(page, QStringLiteral("openAiModel")).apply(settings, QStringLiteral("gpt-5.4"));
        rowById(page, QStringLiteral("anthropicEffort")).apply(settings, QStringLiteral("max"));
        QCOMPARE(settings.refinement.openAiModel, QStringLiteral("gpt-5.4"));
        QCOMPARE(settings.refinement.anthropicEffort, QStringLiteral("max"));

        QCOMPARE(rowById(page, QStringLiteral("openAiFastMode")).value(settings).toBool(), true);
        QCOMPARE(rowById(page, QStringLiteral("anthropicFastMode")).value(settings).toBool(), true);
        rowById(page, QStringLiteral("openAiFastMode")).apply(settings, false);
        rowById(page, QStringLiteral("anthropicFastMode")).apply(settings, false);
        QCOMPARE(settings.refinement.openAiFastMode, false);
        QCOMPARE(settings.refinement.anthropicFastMode, false);

        const SettingsSection &server = page.sections.last();
        QCOMPARE(server.title, QStringLiteral("CLI Proxy API"));
        QCOMPARE(server.rows.size(), 2);
        QCOMPARE(server.rows.at(0).id, QStringLiteral("cliproxyBaseUrl"));
        QCOMPARE(server.rows.at(1).id, QStringLiteral("cliproxyApiKey"));
    }

    void qtProviderPagesCoverEveryProviderRow()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const QStringList covered = SettingsPageSet::providerModelRowIds()
            + SettingsPageSet::providerAuthRowIds();
        for (const SettingsSection &section : schema.page(QStringLiteral("providers")).sections) {
            for (const SettingsRow &row : section.rows) {
                QVERIFY2(covered.contains(row.id),
                         qPrintable(QStringLiteral("row %1 is on no Qt provider page").arg(row.id)));
            }
        }
    }

    void aModelThatReadsTranscriptsAsInstructionsSaysSo()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &caution = rowById(schema.page(QStringLiteral("providers")),
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
        const SettingsRow &row = rowById(schema.page(QStringLiteral("applications")),
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
        const SettingsRow &row = rowById(schema.page(QStringLiteral("corrections")),
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
        const SettingsRow &row = rowById(schema.page(QStringLiteral("corrections")),
                                         QStringLiteral("learnedCorrections"));
        const CollectionColumn &application = row.collection.columns.last();
        QVERIFY(application.recordTooltip);
        QCOMPARE(application.recordTooltip({{QStringLiteral("confidence"), 0.92}}),
                 QStringLiteral("Learned automatically · confidence 92%"));
    }

    void replacementValidationSpeaksForItself()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("bindings")),
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
            rowById(schema.page(QStringLiteral("bindings")), QStringLiteral("bindingRules"))
                .collection.supportsImport;
        const QList<QVariantMap> snippets =
            json.parse(R"([{"phrase": "sign off", "replacement": "Thanks"}])", &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(snippets.size(), 1);
        QCOMPARE(snippets.first().value(QStringLiteral("replacement")).toString(),
                 QStringLiteral("Thanks"));
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
