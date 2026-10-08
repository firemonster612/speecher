#include "core/EndpointSettings.h"
#include "core/SecretStore.h"
#include "common/test_local_setup.h"
#include "common/test_suites.h"

#include "app/NetworkReachability.h"
#include "app/ProviderAvailability.h"
#include "app/SetupSteps.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"

#include "core/BindingProcessor.h"
#include "core/SettingsStore.h"
#include "core/VocabularyLimit.h"
#include "core/ReleaseNotesPresentation.h"
#include "core/settings/FallbackPresentation.h"
#include "core/settings/SettingsSchema.h"
#include "core/settings/SpokenLanguages.h"
#include "transcribe/TranscribePresentation.h"

#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
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

// Every provider of both roles, with their registry labels.
SchemaContext chainContext()
{
    SchemaContext context = fakeContext();
    context.speechProviders = {{QStringLiteral("claude"), QStringLiteral("Claude Voice")},
                               {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")},
                               {QStringLiteral("local"), QStringLiteral("Local Model")},
                               {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint")}};
    context.refinementProviders = {{QStringLiteral("openai"), QStringLiteral("OpenAI"), true},
                                   {QStringLiteral("anthropic"), QStringLiteral("Anthropic"), true},
                                   {QStringLiteral("endpoint"), QStringLiteral("Custom Endpoint"), false},
                                   {QStringLiteral("local"), QStringLiteral("Local Runner"), false}};
    return context;
}

QList<RowOption> speechChoices()
{
    return chainContext().speechProviders;
}

QList<RowOption> refinementChoices()
{
    QList<RowOption> choices;
    for (const RefinementProvider &provider : chainContext().refinementProviders) {
        choices.append({provider.id, provider.label});
    }
    return choices;
}

// A provider's line in `speecher providers`, or an empty report for one the
// registry doesn't offer.
ProviderReport reportOf(ProviderRole role, const QString &id, const AppSettings &settings, const LiveFacts &facts)
{
    const QList<ProviderReport> reports = providerReports(settings, facts, speechChoices(), refinementChoices());
    const auto found = std::find_if(reports.cbegin(), reports.cend(), [&](const ProviderReport &report) {
        return report.role == role && report.id == id;
    });
    return found == reports.cend() ? ProviderReport{} : *found;
}

QStringList ids(const QList<RowOption> &options)
{
    QStringList ids;
    for (const RowOption &option : options) {
        ids.append(option.id);
    }
    return ids;
}

} // namespace

class SettingsSchemaTests : public QObject {
    Q_OBJECT

private slots:
    // Each service lists what the 2026-10-03 probe found it accepts, and a
    // Local Model what transcribe.cpp's catalog says it takes.
    void eachSpeechServiceListsTheSpokenLanguagesItListensFor()
    {
        SpeechSettings speech;
        QVERIFY(spokenLanguages(speech).contains(QStringLiteral("de")));
        QVERIFY(spokenLanguages(speech).contains(QStringLiteral("auto")));
        QVERIFY(!spokenLanguages(speech).contains(QStringLiteral("cy")));
        speech.providerId = QStringLiteral("codex");
        QVERIFY(spokenLanguages(speech).contains(QStringLiteral("cy")));
        speech.providerId = QStringLiteral("local");
        speech.local.modelId = QStringLiteral("parakeet");
        QCOMPARE(spokenLanguages(speech), QStringList{QStringLiteral("en")});
        speech.local.modelId = QStringLiteral("cohere");
        QCOMPARE(spokenLanguages(speech).size(), 14);
        speech.local.modelId = QStringLiteral("qwen3-asr");
        QCOMPARE(spokenLanguages(speech).size(), 31);
        speech.providerId = QStringLiteral("endpoint");
        QCOMPARE(spokenLanguages(speech).size(), 101);
    }

    void spokenLanguagesReadInEnglishThenTheirOwnName()
    {
        QCOMPARE(spokenLanguageLabel(QStringLiteral("de")), QStringLiteral("German (Deutsch)"));
        QCOMPARE(spokenLanguageLabel(QStringLiteral("ja")), QString::fromUtf8("Japanese (日本語)"));
        QCOMPARE(spokenLanguageLabel(QStringLiteral("es")), QString::fromUtf8("Spanish (español)"));
        QCOMPARE(spokenLanguageLabel(QStringLiteral("en")), QStringLiteral("English"));
        QCOMPARE(spokenLanguageLabel(QStringLiteral("auto")), QStringLiteral("Automatic"));
    }

    // A saved language the service lacks stays saved and shown, disabled,
    // with a caution naming the service; nothing turns it into English.
    void aSpokenLanguageTheServiceLacksStaysWithACaution()
    {
        const SettingsPage page = buildSettingsSchema(fakeContext()).page(QStringLiteral("audio"));
        const SettingsRow &row = rowById(page, QStringLiteral("spokenLanguage"));
        const SettingsRow &caution = rowById(page, QStringLiteral("spokenLanguageCaution"));
        AppSettings settings;
        QCOMPARE(row.options(settings).first().label, QStringLiteral("Automatic"));
        QVERIFY(!caution.visible(settings, {}));

        settings.speech.language = QStringLiteral("cy");
        const RowOption saved = row.options(settings).last();
        QCOMPARE(saved.id, QStringLiteral("cy"));
        QCOMPARE(saved.label, QStringLiteral("Welsh (Cymraeg)"));
        QVERIFY(!saved.enabled);
        QVERIFY(caution.visible(settings, {}));
        QCOMPARE(caution.value(settings).toString(),
                 QStringLiteral("Claude Voice can't listen for Welsh. Choose another Spoken Language."));

        settings.speech.providerId = QStringLiteral("local");
        settings.speech.local.modelId = QStringLiteral("parakeet");
        settings.speech.language = QStringLiteral("auto");
        QCOMPARE(spokenLanguageProblem(settings.speech, QStringLiteral("Local Model")),
                 QStringLiteral("Parakeet 0.6B can't detect the language. Choose the language you speak."));
    }

    void refinementServerChoiceSelectsTheCliProxyPreset()
    {
        const auto schema = buildSettingsSchema(fakeContext());
        const auto &page = schema.page("refinement");
        QVERIFY(hasRow(page, "refinementEndpointServer"));
        const auto &server = rowById(page, "refinementEndpointServer");
        AppSettings settings;
        settings.refinement.providerId = "endpoint";
        settings.refinement.cliproxyBaseUrl = "http://proxy.example:8317";
        settings.refinement.cliproxyApiKey = "saved-key";
        const auto options = server.options(settings);
        QCOMPARE(options.size(), 2);
        QCOMPARE(options.at(0).label, QString("Custom"));
        QCOMPARE(options.at(1).label, QString("CLI Proxy API"));
        server.apply(settings, options.at(1).id);
        QCOMPARE(settings.refinement.endpoint.preset, QString("cliproxy"));
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiBase, QString("http://proxy.example:8317/v1"));
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, QString("saved-key"));
        server.apply(settings, options.at(0).id);
        QVERIFY(settings.refinement.endpoint.preset.isEmpty());
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiBase, QString("http://proxy.example:8317/v1"));
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiKey, QString("saved-key"));
        QCOMPARE(settings.refinement.cliproxyApiKey, QString("saved-key"));
    }

    void returningToCustomRestoresTheServerEnteredBefore()
    {
        const auto schema = buildSettingsSchema(fakeContext());
        const auto &server = rowById(schema.page("refinement"), "refinementEndpointServer");
        AppSettings settings;
        settings.refinement.providerId = "endpoint";
        settings.refinement.endpoint.baseUrl = "http://localhost:8080/v1";
        settings.refinement.endpoint.apiKey = "custom-key";
        settings.refinement.cliproxyBaseUrl = "http://proxy.example:8317";
        settings.refinement.cliproxyApiKey = "proxy-key";
        server.apply(settings, "cliproxy");
        server.apply(settings, QString());
        const auto endpoint = resolvedRefinementEndpoint(settings.refinement);
        QCOMPARE(endpoint.apiBase, QString("http://localhost:8080/v1"));
        QCOMPARE(endpoint.apiKey, QString("custom-key"));
    }

    void endpointPresetRowsShowAndEditEffectiveValues()
    {
        const auto schema = buildSettingsSchema(fakeContext());
        const auto &page = schema.page("refinement");
        const auto &url = rowById(page, "refinementEndpointUrl");
        const auto &key = rowById(page, "refinementEndpointApiKey");
        AppSettings settings;
        settings.refinement.endpoint.preset = "cliproxy";
        settings.refinement.cliproxyBaseUrl = "http://proxy.example:8317";
        settings.refinement.cliproxyApiKey = "saved-key";
        QCOMPARE(url.value(settings).toString(), QString("http://proxy.example:8317/v1"));
        QCOMPARE(key.value(settings).toString(), QString("saved-key"));
        url.apply(settings, "http://proxy.example:8317/v1");
        QCOMPARE(settings.refinement.endpoint.preset, QString("cliproxy"));
        url.apply(settings, "http://other.example/v1");
        QVERIFY(settings.refinement.endpoint.preset.isEmpty());
        QCOMPARE(settings.refinement.endpoint.baseUrl, QString("http://other.example/v1"));
        QVERIFY(resolvedRefinementEndpoint(settings.refinement).apiKey.isEmpty());
        AppSettings withKey;
        withKey.refinement.endpoint.preset = "cliproxy";
        withKey.refinement.cliproxyApiKey = "saved-key";
        editRefinementEndpoint(withKey, {.baseUrl = "http://other.example/v1", .apiKey = "other-key"});
        QCOMPARE(resolvedRefinementEndpoint(withKey.refinement).apiKey, QString("other-key"));
    }

    void endpointEditsPreserveLateSecretsAndExplicitClears()
    {
        const auto schema = buildSettingsSchema(fakeContext());
        const auto key = SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey);
        AppSettings loaded;
        loaded.unreadSecretKeys = {key};
        AppSettings draft = loaded;
        editRefinementEndpoint(draft, {.model = "chosen"});
        AppSettings current = loaded;
        current.unreadSecretKeys.clear();
        current.refinement.endpoint.apiKey = "late-key";
        auto merged = mergeSettingsDraft(schema, loaded, draft, current);
        QCOMPARE(merged.refinement.endpoint.apiKey, QString("late-key"));
        QCOMPARE(merged.refinement.endpoint.model, QString("chosen"));
        editRefinementEndpoint(draft, {.apiKey = QString()});
        merged = mergeSettingsDraft(schema, loaded, draft, current);
        QVERIFY(merged.refinement.endpoint.apiKey.isEmpty());
        QVERIFY(!merged.unreadSecretKeys.contains(key));
    }

    // The Qt row applies every keystroke and reads the value back into the
    // field, so a URL typed one character at a time must survive intact.
    void typingTheEndpointUrlKeepsItsSlashes()
    {
        const auto schema = buildSettingsSchema(fakeContext());
        const auto &url = rowById(schema.page("refinement"), "refinementEndpointUrl");
        AppSettings settings;
        settings.refinement.providerId = "endpoint";
        const QString typed = "http://127.0.0.1:9000/v1";
        for (qsizetype i = 1; i <= typed.size(); ++i) {
            const QString shown = url.value(settings).toString();
            url.apply(settings, shown + typed.at(i - 1));
        }
        QCOMPARE(url.value(settings).toString(), typed);
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiBase, typed);
    }

    void detachingAnUnreadProxyUrlDropsTheProxyKey()
    {
        AppSettings settings;
        settings.refinement.endpoint.preset = "cliproxy";
        settings.refinement.cliproxyBaseUrl = "http://proxy.example:8317";
        settings.unreadSecretKeys = {SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey)};
        const AppSettings unread = settings;
        auto cleared = unread;
        editRefinementEndpoint(cleared, {.apiKey = QString()});
        auto late = unread;
        late.refinement.cliproxyApiKey = "late-proxy-key";
        late.unreadSecretKeys.clear();
        const auto merged = mergeSettingsDraft(buildSettingsSchema(fakeContext()), unread, cleared, late);
        QVERIFY(resolvedRefinementEndpoint(merged.refinement).apiKey.isEmpty());
        QCOMPARE(merged.refinement.cliproxyApiKey, QString("late-proxy-key"));
        editRefinementEndpoint(settings, {.baseUrl = "http://other.example/v1"});
        settings.refinement.cliproxyApiKey = "late-proxy-key";
        QVERIFY(resolvedRefinementEndpoint(settings.refinement).apiKey.isEmpty());
        QCOMPARE(resolvedRefinementEndpoint(settings.refinement).apiBase, QString("http://other.example/v1"));
    }

    void writingProfileCollectionIsDescribedBySchema()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page("writingProfiles"), "writingProfileBehavior");
        QCOMPARE(row.kind, RowKind::Custom);
        QCOMPARE(row.collection.identityColumn, QStringLiteral("profileId"));
        QCOMPARE(row.collection.records(AppSettings{}).size(), 5);
    }

    void refinementRowsGoDeadWhenTheProviderIsNone()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        AppSettings off;
        off.refinement.providerId = QStringLiteral("none");
        AppSettings on;
        on.refinement.providerId = QStringLiteral("openai");
        const Capabilities capable{true, false, true};

        // The profiles themselves stay open: one may pick a speech service or
        // refine for itself.
        for (const QString &id : {QStringLiteral("defaultWritingProfile"),
                                  QStringLiteral("writingProfileBehavior")}) {
            const SettingsRow &row = *schema.row(id);
            QVERIFY2(!row.enabled || row.enabled(off, capable), qPrintable(id));
        }
        for (const QString &id : {QStringLiteral("additionalInstructions"),
                                  QStringLiteral("customSystemPromptEnabled")}) {
            const SettingsRow &row = *schema.row(id);
            QVERIFY2(!row.enabled(off, capable), qPrintable(id));
            QVERIFY2(row.enabled(on, capable), qPrintable(id));
            QCOMPARE(row.disabledHelp, QStringLiteral("Refinement is off."));
        }
        // Context only shapes a refinement request, so it goes with refinement.
        for (const QString &id : {QStringLiteral("targetContextControl"),
                                  QStringLiteral("includeScreenshotContext")}) {
            QVERIFY2(!schema.row(id)->visible(off, capable), qPrintable(id));
            QVERIFY2(schema.row(id)->visible(on, capable), qPrintable(id));
        }
    }

    // A row held by two gates names the one that is closed.
    void aDisabledRowNamesTheGateThatIsClosed()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &prompt = *schema.row(QStringLiteral("customSystemPrompt"));
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("none");
        settings.refinement.customSystemPromptEnabled = true;
        QVERIFY(!prompt.enabled(settings, Capabilities{}));
        QCOMPARE(prompt.disabledHelpValue(settings, Capabilities{}), QStringLiteral("Refinement is off."));
        settings.refinement.providerId = QStringLiteral("openai");
        settings.refinement.customSystemPromptEnabled = false;
        QVERIFY(!prompt.enabled(settings, Capabilities{}));
        QCOMPARE(prompt.disabledHelpValue(settings, Capabilities{}),
                 QStringLiteral("Turn on the custom system prompt to edit it."));
        settings.refinement.customSystemPromptEnabled = true;
        QVERIFY(prompt.enabled(settings, Capabilities{}));
    }

    void previewTogglesPersistThroughSchemaDraft()
    {
        SettingsStore store;
        store.raw().clear();
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const auto &page = schema.page(QStringLiteral("general"));
        const auto &transcription = rowById(page, QStringLiteral("transcriptionPreviewEnabled"));
        const auto &refinement = rowById(page, QStringLiteral("refinementPreviewEnabled"));
        const AppSettings loaded = store.snapshot();
        QVERIFY(transcription.value(loaded).toBool());
        QVERIFY(refinement.value(loaded).toBool());
        AppSettings edited = loaded;
        transcription.apply(edited, false);
        refinement.apply(edited, false);
        store.applySnapshot(mergeSettingsDraft(schema, loaded, edited, store.snapshot()));
        const AppSettings saved = SettingsStore().snapshot();
        QVERIFY(!transcription.value(saved).toBool());
        QVERIFY(!refinement.value(saved).toBool());
    }

    void insightsSettingPersistsThroughSchemaDraft()
    {
        SettingsStore store;
        store.raw().clear();
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("insightsEnabled"));
        const AppSettings loaded = store.snapshot();
        QVERIFY(row.value(loaded).toBool());
        AppSettings edited = loaded;
        row.apply(edited, false);
        store.applySnapshot(mergeSettingsDraft(schema, loaded, edited, store.snapshot()));
        QVERIFY(!SettingsStore().insightsEnabled());
    }

    void insightsOffNoteShowsOnlyWhileInsightsAreOff()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("insightsOffNote"));
        QCOMPARE(row.kind, RowKind::Info);
        AppSettings settings;
        settings.insightsEnabled = true;
        QVERIFY(!row.visible(settings, Capabilities{}));
        settings.insightsEnabled = false;
        QVERIFY(row.visible(settings, Capabilities{}));
        QCOMPARE(row.label,
                 QStringLiteral("Nothing new is recorded while this is off. History you already "
                                "have stays until you clear it."));
    }

    void staleDraftPreservesLearnedRecords()
    {
        SettingsStore store;
        store.raw().clear();
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const AppSettings loaded = store.snapshot();
        AppSettings edited = loaded;
        edited.ui.previewWords = 12;
        QVERIFY(store.recordCorrectionEvidence({"githab", "GitHub", 0.95}, "editor"));
        const AppSettings learned = store.snapshot();
        QVERIFY(!learned.learnedCorrections.isEmpty());
        store.applySnapshot(mergeSettingsDraft(schema, loaded, edited, store.snapshot()));
        QCOMPARE(store.previewWords(), 12);
        QCOMPARE(store.snapshot().learnedCorrections, learned.learnedCorrections);
        QCOMPARE(store.snapshot().vocabulary, learned.vocabulary);
    }

    void collectionEditPreservesConcurrentEvidenceAndDeletions()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        AppSettings loaded;
        loaded.learnedCorrections = {{"one", "githab", "GitHub", "editor", 100, 0.8, true, 1, 100},
                                     {"two", "cut", "deleted", "editor", 100, 0.8, true, 1, 100}};
        loaded.vocabulary = {{"GitHub", "learned", false, 1, 100}};
        AppSettings edited = loaded;
        edited.learnedCorrections[0].enabled = false;
        edited.learnedCorrections.removeAt(1);
        edited.vocabulary[0].starred = true;
        AppSettings current = loaded;
        current.learnedCorrections[0].evidenceCount = 3;
        current.learnedCorrections[0].lastObservedAtMs = 300;
        current.learnedCorrections.append({"three", "new", "newer", "editor", 300, 0.9, true, 1, 300});
        current.vocabulary[0].frequency = 5;
        current.vocabulary[0].lastUsedMs = 300;
        current.ui.previewWords = 15;
        const AppSettings merged = mergeSettingsDraft(schema, loaded, edited, current);
        QCOMPARE(merged.learnedCorrections.size(), 2);
        QCOMPARE(merged.learnedCorrections[0].id, QStringLiteral("one"));
        QVERIFY(!merged.learnedCorrections[0].enabled);
        QCOMPARE(merged.learnedCorrections[0].evidenceCount, 3);
        QCOMPARE(merged.learnedCorrections[0].lastObservedAtMs, 300);
        QCOMPARE(merged.learnedCorrections[1].id, QStringLiteral("three"));
        QVERIFY(merged.vocabulary[0].starred);
        QCOMPARE(merged.vocabulary[0].frequency, 5);
        QCOMPARE(merged.vocabulary[0].lastUsedMs, 300);
        QCOMPARE(merged.ui.previewWords, 15);
    }

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

    void releaseNotesShowOnlyThisPlatformsBullets()
    {
        const QString notes = QStringLiteral("## Added\n\n- A shared change that wraps\n  onto a second line.\n"
                                             "- Linux: the tray.\n\n## Fixed\n\n- Windows: a fix.");
        QCOMPARE(releaseNotesForPlatform(notes, QStringLiteral("macOS")),
                 QStringLiteral("## Added\n\n- A shared change that wraps onto a second line."));
    }

    void releaseNotesDropThisPlatformsPrefixAndKeepNestedBullets()
    {
        const QString notes = QStringLiteral("- Linux: the tray, with\n  two parts:\n  - a menu\n  - a panel\n"
                                             "- Windows: a fix:\n  - its detail");
        QCOMPARE(releaseNotesForPlatform(notes, QStringLiteral("Linux")),
                 QStringLiteral("- The tray, with two parts:\n  - a menu\n  - a panel"));
    }

    // Custom keeps the interval and reads it back in the largest unit that
    // divides it, the unit given stays, and a listed frequency chosen later
    // wins over the hidden custom row that front ends still apply.
    void customCheckIntervalKeepsTheUnitItWasGivenIn()
    {
        const SettingsPage page = buildSettingsSchema(fakeContext()).page(QStringLiteral("general"));
        const SettingsRow &frequency = rowById(page, QStringLiteral("updateCheckInterval"));
        const SettingsRow &custom = rowById(page, QStringLiteral("updateCheckCustomInterval"));
        AppSettings settings;
        QStringList labels;
        for (const RowOption &option : frequency.options(settings)) {
            labels.append(option.label);
        }
        QCOMPARE(labels,
                 (QStringList{"Every 5 minutes", "Every 15 minutes", "Every 30 minutes", "Every hour",
                              "Every 6 hours", "Every 12 hours", "Every day", "Every week", "Custom"}));
        QCOMPARE(frequency.value(settings).toString(), QStringLiteral("30"));
        QVERIFY(!custom.visible(settings, {}));

        settings.updates.checkIntervalMinutes = 1440;
        frequency.apply(settings, QStringLiteral("custom"));
        QVERIFY(custom.visible(settings, {}));
        QCOMPARE(custom.value(settings).toMap(),
                 (QVariantMap{{QStringLiteral("number"), 1}, {QStringLiteral("unit"), QStringLiteral("days")}}));

        custom.apply(settings, QVariantMap{{QStringLiteral("number"), 60}, {QStringLiteral("unit"), QStringLiteral("minutes")}});
        QCOMPARE(settings.updates.checkIntervalMinutes, 60);
        QCOMPARE(frequency.value(settings).toString(), QStringLiteral("custom"));
        QCOMPARE(custom.value(settings).toMap(),
                 (QVariantMap{{QStringLiteral("number"), 60}, {QStringLiteral("unit"), QStringLiteral("minutes")}}));

        custom.apply(settings, QVariantMap{{QStringLiteral("number"), 30}, {QStringLiteral("unit"), QStringLiteral("days")}});
        QCOMPARE(settings.updates.checkIntervalMinutes, 43200);

        frequency.apply(settings, QStringLiteral("10080"));
        custom.apply(settings, QVariantMap{{QStringLiteral("number"), 8}, {QStringLiteral("unit"), QStringLiteral("hours")}});
        QCOMPARE(settings.updates.checkIntervalMinutes, 10080);
        QCOMPARE(frequency.value(settings).toString(), QStringLiteral("10080"));
        QVERIFY(!custom.visible(settings, {}));

        // A unit the row does not offer, such as a hand-edited "weeks", is
        // neither kept nor read back.
        frequency.apply(settings, QStringLiteral("custom"));
        custom.apply(settings, QVariantMap{{QStringLiteral("number"), 2}, {QStringLiteral("unit"), QStringLiteral("weeks")}});
        QCOMPARE(settings.updates.checkIntervalMinutes, 10080);
        settings.updates.checkIntervalUnit = QStringLiteral("weeks");
        QCOMPARE(custom.value(settings).toMap(),
                 (QVariantMap{{QStringLiteral("number"), 7}, {QStringLiteral("unit"), QStringLiteral("days")}}));
    }

    void whatsNewShowsTheNewCheckFrequencies()
    {
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.2.0");
        context.currentVersion = QStringLiteral("0.2.1");
        QVERIFY(hasRow(buildSettingsSchema(context).page(QStringLiteral("whatsNew")),
                       QStringLiteral("updateCheckInterval")));
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
        // Crossing 0.1.x Stable Releases keeps their notes on the page, after
        // the nightly's own section.
        QVERIFY(notes.startsWith(
            QStringLiteral("# Speecher 0.2.0-nightly.20260901+gdef5678")));
        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.1.1")));
    }

    void nightlyCrossingAStableReleaseKeepsItsNotes()
    {
        // The patch+1 scheme makes a pre-release nightly share its base with
        // the Stable Release it precedes: 0.1.5-nightly ships before v0.1.5,
        // and the first nightly after the tag is 0.1.6-nightly.
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.1.5-nightly.20260831+gabc1234");
        context.currentVersion = QStringLiteral("0.1.6-nightly.20260901+gdef5678");
        const QString notes = rowById(buildSettingsSchema(context).page(
                                          QStringLiteral("whatsNew")),
                                      QStringLiteral("whatsNewNotes"))
                                  .value(AppSettings{})
                                  .toString();

        QVERIFY(notes.startsWith(
            QStringLiteral("# Speecher 0.1.6-nightly.20260901+gdef5678")));
        QVERIFY(notes.contains(QStringLiteral("# Speecher 0.1.5")));
        QVERIFY(!notes.contains(QStringLiteral("# Speecher 0.1.4")));
    }

    void nightlyNotesLeadWithTheNightlyBuildNotTheStableFallback()
    {
        SchemaContext context = fakeContext();
        context.lastSeenVersion = QStringLiteral("0.1.6-nightly.20260831+gabc1234");
        context.currentVersion = QStringLiteral("0.1.6-nightly.20260901+gdef5678");
        const QString notes = rowById(buildSettingsSchema(context).page(
                                          QStringLiteral("whatsNew")),
                                      QStringLiteral("whatsNewNotes"))
                                  .value(AppSettings{})
                                  .toString();

        QVERIFY(notes.startsWith(
            QStringLiteral("# Speecher 0.1.6-nightly.20260901+gdef5678")));
        QVERIFY(!notes.contains(QStringLiteral("# Speecher 0.1.5")));
        QVERIFY(notes.contains(QStringLiteral("compare/abc1234...def5678")));
    }

    void nightlyChangesListCommitsSinceThePreviousNightly()
    {
        const QChar rs(0x1e);
        const QChar us(0x1f);
        QString history;
        history += rs
            + QStringLiteral("def5678") + us
            + QStringLiteral("Merge pull request #91 from firemonster612/feat/thing") + us
            + QStringLiteral("Add the thing\n\nA longer description.\n");
        history += rs
            + QStringLiteral("bbb1111") + us
            + QStringLiteral("Fix crash on empty transcript (#90)") + us + QStringLiteral("\n");
        history += rs
            + QStringLiteral("ccc2222") + us
            + QStringLiteral("Tidy the tray icon") + us + QStringLiteral("\n");
        history += rs
            + QStringLiteral("eee4444") + us
            + QStringLiteral("Merge pull request #92 from firemonster612/no-body") + us
            + QStringLiteral("\n");
        history += rs
            + QStringLiteral("abc1234") + us
            + QStringLiteral("Merge pull request #88 from firemonster612/old") + us
            + QStringLiteral("Older work\n");

        const QString markdown = nightlyChangesMarkdown(
            history,
            QStringLiteral("0.1.6-nightly.20260831+gabc1234"),
            QStringLiteral("0.1.6-nightly.20260901+gdef5678"));

        QVERIFY(markdown.contains(QStringLiteral(
            "- Add the thing ([#91](https://github.com/firemonster612/speecher/pull/91))")));
        QVERIFY(markdown.contains(QStringLiteral(
            "- Fix crash on empty transcript ([#90](https://github.com/firemonster612/speecher/pull/90))")));
        QVERIFY(markdown.contains(QStringLiteral(
            "- Tidy the tray icon ([ccc2222](https://github.com/firemonster612/speecher/commit/ccc2222))")));
        // A merge whose body carries no pull request title keeps the subject.
        QVERIFY(markdown.contains(QStringLiteral(
            "- Merge pull request #92 from firemonster612/no-body "
            "([#92](https://github.com/firemonster612/speecher/pull/92))")));
        QVERIFY(!markdown.contains(QStringLiteral("Older work")));
        QVERIFY(markdown.endsWith(QStringLiteral(
            "[Compare commits](https://github.com/firemonster612/speecher/compare/abc1234...def5678)")));
    }

    void nightlyChangesFallBackToTheCompareLinkAlone()
    {
        const QString outsideWindow = nightlyChangesMarkdown(
            QString(QChar(0x1e)) + QStringLiteral("ddd9999") + QChar(0x1f)
                + QStringLiteral("Unrelated") + QChar(0x1f),
            QStringLiteral("0.1.6-nightly.20260831+gabc1234"),
            QStringLiteral("0.1.6-nightly.20260901+gdef5678"));
        QCOMPARE(outsideWindow,
                 QStringLiteral("[Compare commits]"
                                "(https://github.com/firemonster612/speecher/compare/abc1234...def5678)"));

        QVERIFY(nightlyChangesMarkdown({},
                                       QStringLiteral("0.1.6-nightly.20260831+gabc1234"),
                                       QStringLiteral("0.1.7"))
                    .isEmpty());
        QVERIFY(nightlyChangesMarkdown({},
                                       QStringLiteral("0.1.5"),
                                       QStringLiteral("0.1.6-nightly.20260901+gdef5678"))
                    .isEmpty());
    }

    void rowsRoundTripAValueThroughAppSettings()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &previewWords = rowById(schema.page(QStringLiteral("general")),
                                                  QStringLiteral("previewWords"));
        const SettingsRow &captureMode = rowById(schema.page(QStringLiteral("audio")),
                                                 QStringLiteral("captureMode"));
        const SettingsRow &profiles = rowById(schema.page(QStringLiteral("writingProfiles")),
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

    void activationModeRowOffersThreeModesAndDefaultsToHybrid()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("activationMode"));
        QCOMPARE(row.kind, RowKind::Choice);
        QStringList ids;
        for (const RowOption &option : row.options(AppSettings{})) {
            ids.append(option.id);
        }
        QCOMPARE(ids,
                 QStringList({QStringLiteral("push_to_talk"),
                              QStringLiteral("toggle"),
                              QStringLiteral("hybrid")}));
        QCOMPARE(row.value(AppSettings{}).toString(), QStringLiteral("hybrid"));

        AppSettings settings;
        row.apply(settings, QStringLiteral("toggle"));
        QCOMPARE(settings.shortcutActivationMode, ShortcutActivationMode::Toggle);

        // Both held modes need the desktop to report the key going up, and
        // some Linux desktops never do; the row says so rather than offering a
        // mode that quietly behaves as another one.
#ifdef Q_OS_LINUX
        QVERIFY(row.tooltip.contains(QStringLiteral("behave as Toggle")));
#endif
    }

    void launchAtLoginAppearsOnMacOSAndWindows()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
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

    // The toggle saves whether or not the computer honours it, so a refusal
    // has nowhere else to appear.
    void launchAtLoginCautionAppearsOnlyAfterARefusal()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        const SettingsRow &row = rowById(general, QStringLiteral("launchAtLoginProblem"));
        QCOMPARE(row.kind, RowKind::Info);
        QVERIFY(row.visible);
        QVERIFY(!row.visible(AppSettings{}, Capabilities{}));
        Capabilities refused;
        refused.launchAtLoginAccepted = false;
        QVERIFY(row.visible(AppSettings{}, refused));
        QVERIFY(row.value(AppSettings{}).toString().contains(QStringLiteral("start at login")));
#else
        QVERIFY(!hasRow(general, QStringLiteral("launchAtLoginProblem")));
#endif
    }

    void audioTimingControlsSitUnderRecordingInPlainWords()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &audio = schema.page(QStringLiteral("audio"));
        const SettingsSection &recording = audio.sections.last();
        QCOMPARE(recording.title, QStringLiteral("Recording"));
        QStringList ids;
        for (const SettingsRow &row : recording.rows) {
            ids.append(row.id);
        }
        QCOMPARE(ids,
                 QStringList({QStringLiteral("vadEnabled"),
                              QStringLiteral("vadThresholdPercent"),
                              QStringLiteral("preRollMs"),
                              QStringLiteral("postRollMs")}));
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
        QCOMPARE(row.actionLabel, QStringLiteral("Remove Speecher…"));
        QCOMPARE(general.sections.last().title, QStringLiteral("Uninstall"));
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

    void everyDeletableCollectionCanUndoADelete()
    {
        // No editor asks before it deletes, so each one must be able to put a
        // record back, and must say what an empty table means.
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    if (row.kind != RowKind::Collection) {
                        continue;
                    }
                    const QList<RowOption> &actions = row.collection.actions;
                    QVERIFY2(std::any_of(actions.cbegin(), actions.cend(),
                                         [](const RowOption &action) { return action.id == QStringLiteral("undoDelete"); }),
                             qPrintable(row.id));
                    const bool alwaysHasRecords = row.collection.lockedRecordCount
                        && row.collection.lockedRecordCount() > 0;
                    QVERIFY2(alwaysHasRecords || !row.collection.emptyTitle.isEmpty(), qPrintable(row.id));
                }
            }
        }
    }

    void themeRowExplainsItselfWhenTheDesktopIgnoresIt()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
#ifdef Q_OS_MACOS
        QVERIFY(!schema.row(QStringLiteral("themeControl")));
        return;
#endif
        const SettingsRow &row = rowById(schema.page(QStringLiteral("general")),
                                         QStringLiteral("themeControl"));
        Capabilities honoured;
        QVERIFY(row.enabled(AppSettings{}, honoured));
        Capabilities ignored;
        ignored.colorSchemeOverride = false;
        QVERIFY(!row.enabled(AppSettings{}, ignored));
        QVERIFY(row.disabledHelp.contains(QStringLiteral("desktop")));
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
                 QStringList({QStringLiteral("audioDevice"), QStringLiteral("openAiAuth"),
                              QStringLiteral("anthropicAuth")}));
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
        // Each provider is an account section (sign-in, status), then the
        // shared CLI Proxy API server.
        QCOMPARE(page.sections.size(), 3);
        QCOMPARE(page.sections.at(0).title, QStringLiteral("OpenAI"));
        QCOMPARE(page.sections.at(1).title, QStringLiteral("Anthropic"));
        for (int index : {0, 1}) {
            QCOMPARE(page.sections.at(index).rows.last().label, QStringLiteral("Status"));
        }

        // Model, Effort and Speed sit under Refinement's Provider picker and
        // show only for the provider chosen there.
        const SettingsPage &refinement = schema.page(QStringLiteral("refinement"));
        AppSettings openAi;
        openAi.refinement.providerId = QStringLiteral("openai");
        for (const QString &id : {QStringLiteral("openAiModel"), QStringLiteral("openAiEffort"),
                                  QStringLiteral("openAiSpeed")}) {
            QVERIFY2(rowById(refinement, id).visible(openAi, Capabilities{}), qPrintable(id));
        }
        for (const QString &id : {QStringLiteral("anthropicModel"), QStringLiteral("anthropicEffort"),
                                  QStringLiteral("anthropicFastMode")}) {
            QVERIFY2(!rowById(refinement, id).visible(openAi, Capabilities{}), qPrintable(id));
        }
        QCOMPARE(rowById(refinement, QStringLiteral("anthropicModel")).label, QStringLiteral("Model"));
        QCOMPARE(rowById(refinement, QStringLiteral("anthropicEffort")).label, QStringLiteral("Thinking"));

        AppSettings settings;
        rowById(refinement, QStringLiteral("openAiModel")).apply(settings, QStringLiteral("gpt-5.4"));
        rowById(refinement, QStringLiteral("anthropicEffort")).apply(settings, QStringLiteral("max"));
        QCOMPARE(settings.refinement.openAiModel, QStringLiteral("gpt-5.4"));
        QCOMPARE(settings.refinement.anthropicEffort, QStringLiteral("max"));

        // Both providers' speed is the same Speed choice; Anthropic's stays a flag.
        const SettingsRow &speed = rowById(refinement, QStringLiteral("openAiSpeed"));
        const SettingsRow &anthropicSpeed = rowById(refinement, QStringLiteral("anthropicFastMode"));
        QCOMPARE(speed.value(settings).toString(), QStringLiteral("fast"));
        QCOMPARE(anthropicSpeed.kind, RowKind::Choice);
        QCOMPARE(anthropicSpeed.value(settings).toString(), QStringLiteral("fast"));
        speed.apply(settings, QStringLiteral("ultrafast"));
        anthropicSpeed.apply(settings, QStringLiteral("standard"));
        QCOMPARE(settings.refinement.openAiSpeed, QStringLiteral("ultrafast"));
        QCOMPARE(settings.refinement.anthropicFastMode, false);
        // Ultrafast is only open on GPT-6 Astra.
        QVERIFY(!speed.options(settings).last().enabled);
        settings.refinement.openAiModel = QStringLiteral("gpt-6-astra");
        QVERIFY(speed.options(settings).last().enabled);

        const SettingsSection &server = page.sections.last();
        QCOMPARE(server.title, QStringLiteral("CLI Proxy API"));
        QCOMPARE(server.rows.size(), 3);
        QCOMPARE(server.rows.at(0).id, QStringLiteral("cliproxyOauthDir"));
        QCOMPARE(server.rows.at(1).id, QStringLiteral("cliproxyBaseUrl"));
        QCOMPARE(server.rows.at(2).id, QStringLiteral("cliproxyApiKey"));

        AppSettings oauthDirDraft;
        rowById(page, QStringLiteral("cliproxyOauthDir"))
            .apply(oauthDirDraft, QStringLiteral(" /custom/dir "));
        QCOMPARE(oauthDirDraft.refinement.cliproxyOauthDirConfigured, QStringLiteral("/custom/dir"));
    }

    void accountRowsSpeakOfSignInNotCredentialSources()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &page = schema.page(QStringLiteral("providers"));
        for (const SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                QVERIFY2(!row.label.contains(QStringLiteral("auth"), Qt::CaseInsensitive),
                         qPrintable(row.label));
                QVERIFY2(!row.help.contains(QStringLiteral("OPENAI_API_KEY")), qPrintable(row.id));
                QVERIFY2(!row.help.contains(QStringLiteral("credential"), Qt::CaseInsensitive),
                         qPrintable(row.id));
            }
            // The closing note is one sentence, not a five-source fallback chain.
            QVERIFY2(section.help.count(QStringLiteral(". ")) == 0, qPrintable(section.help));
        }
        QCOMPARE(rowById(page, QStringLiteral("openAiAuthMode")).label, QStringLiteral("Sign-in"));
        const SettingsRow &status = rowById(page, QStringLiteral("openAiAuth"));
        QCOMPARE(status.label, QStringLiteral("Status"));
        // In key mode the row holds the key, and says so.
        AppSettings keyMode;
        keyMode.refinement.openAiAuthMode = QStringLiteral("settings");
        QCOMPARE(status.labelValue(keyMode), QStringLiteral("API key"));
    }

    void aModelThatReadsTranscriptsAsInstructionsSaysSo()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &caution = rowById(schema.page(QStringLiteral("refinement")),
                                             QStringLiteral("anthropicModelCaution"));
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("anthropic");
        QVERIFY(!caution.visible(settings, Capabilities{}));
        settings.refinement.anthropicModel = QStringLiteral("claude-haiku-4-5");
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
        const SettingsRow &browsers = rowById(schema.page(QStringLiteral("output")),
                                              QStringLiteral("categoryPasteRule_browser"));
        AppSettings settings;
        settings.output.pasteRules = {
            {PasteRuleScope::Category, QStringLiteral("unknown"), PasteMethod::ClipboardOnly, true},
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };

        QCOMPARE(browsers.value(settings).toString(), QStringLiteral("inherit"));
        browsers.apply(settings, QStringLiteral("clipboard_only"));
        QCOMPARE(browsers.value(settings).toString(), QStringLiteral("clipboard_only"));
        browsers.apply(settings, QStringLiteral("inherit"));
        QCOMPARE(settings.output.pasteRules.size(), 2);
        QCOMPARE(settings.output.pasteRules.first().match, QStringLiteral("unknown"));
    }

    // A front end shows each choice's value and saves what it shows, so a
    // migrated rule survives a save only if its row offers that value.
    void migratedPasteRulesSurviveSavingTheOutputPage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings stored(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        stored.setValue(QStringLiteral("output/method"), QStringLiteral("direct_insert"));
        stored.setValue(QStringLiteral("output/pasteRules"),
                        pasteRulesToJson({
                            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::TerminalPaste, true},
                            {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
                        }));
        migrateOutputMethod(stored);
        AppSettings loaded;
        loaded.output.method = stored.value(QStringLiteral("output/method")).toString();
        loaded.output.pasteRules =
            pasteRulesFromJson(stored.value(QStringLiteral("output/pasteRules")).toByteArray());

        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsPage &output = schema.page(QStringLiteral("output"));
        AppSettings saved = loaded;
        rowById(output, QStringLiteral("outputFormat")).apply(saved, QStringLiteral("html"));
        for (const SettingsSection &section : output.sections) {
            for (const SettingsRow &row : section.rows) {
                if (!row.options || !row.value || !row.apply) {
                    continue;
                }
                const QString shown = row.value(loaded).toString();
                const QList<RowOption> options = row.options(loaded);
                QVERIFY2(std::any_of(options.cbegin(), options.cend(),
                                     [&shown](const RowOption &option) { return option.id == shown; }),
                         qPrintable(row.id + QStringLiteral(" does not offer ") + shown));
                row.apply(saved, shown);
            }
        }
        QCOMPARE(saved.output.method, QStringLiteral("automatic"));
        QCOMPARE(saved.output.pasteRules,
                 (QList<PasteRule>{
                     {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::DirectInsert, true},
                     {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
                 }));
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
                 VocabularyLimit::summary({QStringLiteral("Speecher")}, QStringLiteral("claude")));

        // It counts what the speech provider in use receives: local models get no hints.
        settings.speech.providerId = QStringLiteral("local");
        QCOMPARE(limit.value(settings).toString(), QStringLiteral("1 term, used for refinement"));

        // The Key term icon follows the terms the speech request is cut from:
        // the starred one is sent even at the bottom, and takes a slot, so
        // the 100th unstarred term is faint. A term that is not a key term
        // has none.
        settings.speech.providerId = QStringLiteral("claude");
        QList<QVariantMap> shown;
        for (int index = 0; index < 101; ++index) {
            shown.append({{QStringLiteral("term"), QStringLiteral("term%1").arg(index, 3, 10, QLatin1Char('0'))}});
        }
        shown.append({{QStringLiteral("term"), QStringLiteral("late")}, {QStringLiteral("starred"), true}});
        shown.append({{QStringLiteral("term"), QStringLiteral("refined")}, {QStringLiteral("keyTerm"), false}});
        const auto column = [&row](const QString &id) {
            return *std::find_if(row.collection.columns.cbegin(), row.collection.columns.cend(),
                                 [&id](const CollectionColumn &column) { return column.id == id; });
        };
        const QList<IconCell> sent = column(QStringLiteral("keyTermIcon")).icons(shown, settings);
        QCOMPARE(sent.size(), 103);
        QCOMPARE(sent.first().state, IconCell::State::Shown);
        QCOMPARE(sent.at(98).state, IconCell::State::Shown);
        QCOMPARE(sent.at(99).state, IconCell::State::Faint);
        QCOMPARE(sent.at(101).state, IconCell::State::Shown);
        QCOMPARE(sent.last().state, IconCell::State::None);
        // A speech service that takes no key terms sends none of them.
        settings.speech.providerId = QStringLiteral("codex");
        QCOMPARE(column(QStringLiteral("keyTermIcon")).icons(shown, settings).first().state, IconCell::State::Faint);

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

    void restoreClipboardIsDescribedOnceForEverySurface()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("output")),
                                         QStringLiteral("restoreClipboardAfterTyping"));
        QCOMPARE(row.help, restoreClipboardDescription());
        QVERIFY(row.tooltip.isEmpty());
        // Restore now happens only after a confirmed paste; there is no longer
        // a timed restore to describe.
        QCOMPARE(restoreClipboardDescription(),
                 QStringLiteral("If Speecher cannot confirm the paste, your dictation stays on the clipboard."));
    }

    // Writing Profiles holds the profiles, then what they choose from behind
    // one dialog each, ending with the custom prompt, gated like the other
    // rows; the prompt shows the built-in one until something is stored.
    void writingProfilesPaneCarriesInstructionsAndTheCustomPrompt()
    {
        SchemaContext context = fakeContext();
        context.builtInSystemPrompt = QStringLiteral("Built-in rules.");
        const SettingsSchema schema = buildSettingsSchema(context);
        QStringList groups;
        for (const SettingsPaneGroup &group : schema.pane(QStringLiteral("writingProfiles"))->groups) {
            groups.append(group.title + QLatin1Char(':') + group.rows.join(QLatin1Char(',')));
        }
        QCOMPARE(groups,
                 (QStringList{QStringLiteral("Profiles:defaultWritingProfile,writingProfileBehavior"),
                              QStringLiteral("Advanced:customTones,customCleanupLevels,additionalInstructions,"
                                             "customSystemPromptEnabled,resetCustomSystemPrompt,"
                                             "customSystemPrompt")}));
        QStringList dialogs;
        for (const QString &id : schema.pane(QStringLiteral("writingProfiles"))->groups.last().rows) {
            dialogs.append(schema.row(id)->dialog.title);
        }
        QCOMPARE(dialogs, (QStringList{QStringLiteral("Your tones"), QStringLiteral("Your cleanup levels"),
                                       QStringLiteral("Instructions for every profile"),
                                       QStringLiteral("Custom system prompt"), QStringLiteral("Custom system prompt"),
                                       QStringLiteral("Custom system prompt")}));
        const SettingsRow &instructions = *schema.row(QStringLiteral("additionalInstructions"));
        const SettingsRow &prompt = *schema.row(QStringLiteral("customSystemPrompt"));
        QVERIFY(instructions.multiline);
        QVERIFY(prompt.multiline);
        const QStringList gridColumns = [&schema] {
            QStringList ids;
            for (const CollectionColumn &column : schema.row(QStringLiteral("writingProfileBehavior"))->collection.columns) {
                ids.append(column.id + (column.multiline ? QStringLiteral("*") : QString()));
            }
            return ids;
        }();
        QCOMPARE(gridColumns, (QStringList{QStringLiteral("profile"), QStringLiteral("cleanup"),
                                           QStringLiteral("tone"), QStringLiteral("instructions*"),
                                           QStringLiteral("outputLanguage"), QStringLiteral("profileSpeech"),
                                           QStringLiteral("profileSpeechModel"), QStringLiteral("profileRefinement"),
                                           QStringLiteral("profileRefinementModel")}));

        AppSettings settings;
        QCOMPARE(instructions.dialog.summary(settings), QStringLiteral("None"));
        settings.refinement.additionalInstructions = QStringLiteral("Spell it Speecher.\nNo emoji.");
        QCOMPARE(instructions.dialog.summary(settings), QStringLiteral("Spell it Speecher."));
        QCOMPARE(prompt.dialog.summary(settings), QStringLiteral("Off"));
        QCOMPARE(prompt.value(settings).toString(), QStringLiteral("Built-in rules."));
        prompt.apply(settings, QStringLiteral("Mine."));
        QCOMPARE(prompt.value(settings).toString(), QStringLiteral("Mine."));
        settings.refinement.providerId = QStringLiteral("none");
        QVERIFY(!instructions.enabled(settings, {}));
        QVERIFY(!prompt.enabled(settings, {}));

        // A custom prompt still adds a custom level's instructions, so custom
        // levels stay editable while it is on.
        settings.refinement.providerId = QStringLiteral("openai");
        settings.refinement.customSystemPromptEnabled = true;
        QVERIFY(schema.row(QStringLiteral("customCleanupLevels"))->enabled(settings, {}));
    }

    void customChoiceIdsAreSlugsOfTheName()
    {
        QCOMPARE(customChoiceId(QStringLiteral(" Very Terse! "), {}), QStringLiteral("custom_very_terse_"));
        QCOMPARE(customChoiceId(QStringLiteral("Terse"),
                                {QStringLiteral("custom_terse"), QStringLiteral("custom_terse_2")}),
                 QStringLiteral("custom_terse_3"));
    }

    // The collections hold only what the person added; a record gets an id
    // from its name once, and validation names what is wrong.
    void tonesAndCleanupLevelsAreCollectionsOfTheCustomOnes()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &toneRow = *schema.row(QStringLiteral("customTones"));
        const CollectionDescriptor &tones = toneRow.collection;
        const CollectionDescriptor &levels = schema.row(QStringLiteral("customCleanupLevels"))->collection;

        AppSettings settings;
        QCOMPARE(tones.records(settings), QList<QVariantMap>());
        QCOMPARE(toneRow.dialog.summary(settings), QStringLiteral("Add a voice you can pick in any profile."));
        QList<QVariantMap> records = tones.records(settings);
        records.append({{QStringLiteral("name"), QStringLiteral("Terse")},
                        {QStringLiteral("instruction"), QStringLiteral("Short.")}});
        tones.apply(settings, records);
        QCOMPARE(settings.refinement.customTones,
                 (QList<CustomTone>{{QStringLiteral("custom_terse"), QStringLiteral("Terse"),
                                     QStringLiteral("Short.")}}));
        QCOMPARE(toneRow.dialog.summary(settings),
                 QStringLiteral("Add a voice you can pick in any profile. You have 1."));
        records = levels.records(settings);
        records.append({{QStringLiteral("name"), QStringLiteral("Notes")},
                        {QStringLiteral("base"), QStringLiteral("custom_only")},
                        {QStringLiteral("instructions"), QStringLiteral("Bullets.")}});
        levels.apply(settings, records);
        QCOMPARE(settings.refinement.customCleanupLevels,
                 (QList<CustomCleanupLevel>{{QStringLiteral("custom_notes"), QStringLiteral("Notes"),
                                             QStringLiteral("custom_only"), QStringLiteral("Bullets.")}}));

        QCOMPARE(tones.validate({{{QStringLiteral("name"), QStringLiteral(" ")},
                                  {QStringLiteral("instruction"), QStringLiteral(" ")}},
                                 {{QStringLiteral("name"), QStringLiteral("casual")},
                                  {QStringLiteral("instruction"), QStringLiteral("x")}}}),
                 (QStringList{QStringLiteral("Every tone needs a name."),
                              QStringLiteral("There is already a tone named casual."),
                              QStringLiteral("Every tone needs an instruction.")}));
        QCOMPARE(levels.validate({{{QStringLiteral("name"), QStringLiteral("Notes")},
                                   {QStringLiteral("base"), QStringLiteral("custom_only")}},
                                  {{QStringLiteral("name"), QStringLiteral("notes")},
                                   {QStringLiteral("base"), QStringLiteral("balanced")}}}),
                 (QStringList{QStringLiteral("There is already a cleanup level named notes."),
                              QStringLiteral("A Custom only cleanup level needs instructions.")}));
    }

    // The grid lists the built-ins locked, then the custom profiles; one added
    // gets an id from its name, and every profile choice offers it.
    void customProfilesFollowTheBuiltInsInEveryProfileChoice()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const CollectionDescriptor &grid = schema.row(QStringLiteral("writingProfileBehavior"))->collection;
        QCOMPARE(grid.lockedRecordCount(), 5);
        AppSettings settings;
        QList<QVariantMap> records = grid.records(settings);
        records.append({{QStringLiteral("profile"), QStringLiteral(" Stand up ")},
                        {QStringLiteral("cleanup"), QStringLiteral("light_cleanup")},
                        {QStringLiteral("tone"), QStringLiteral("none")},
                        {QStringLiteral("outputLanguage"), QStringLiteral(" Spanish ")},
                        {QStringLiteral("instructions"), QString()}});
        grid.apply(settings, records);
        QCOMPARE(settings.refinement.writingProfiles.last(),
                 (WritingProfileSettings{QStringLiteral("custom_stand_up"), QStringLiteral("light_cleanup"),
                                         QStringLiteral("none"), QString(), QStringLiteral("Stand up"),
                                         QStringLiteral("Spanish")}));

        const auto ids = [](const QList<RowOption> &options) {
            QStringList ids;
            for (const RowOption &option : options) {
                ids.append(option.id + QLatin1Char('=') + option.label);
            }
            return ids;
        };
        const QStringList profiles{QStringLiteral("work=Work"), QStringLiteral("email=Email"),
                                   QStringLiteral("personal=Personal"), QStringLiteral("ai_coding=AI coding"),
                                   QStringLiteral("other=Other"), QStringLiteral("custom_stand_up=Stand up")};
        QCOMPARE(ids(schema.row(QStringLiteral("defaultWritingProfile"))->options(settings)), profiles);
        const CollectionColumn &ruleProfile =
            schema.row(QStringLiteral("appRecognitionRules"))->collection.columns.at(2);
        QCOMPARE(ids(ruleProfile.options(settings)), QStringList{QStringLiteral("=Automatic")} + profiles);
    }

    // Before a profile goes, the person is told what points at it.
    void deletingAProfileSaysWhatItChanges()
    {
        AppSettings settings;
        const QString standup = QStringLiteral("custom_standup");
        QCOMPARE(writingProfileDeletionNotice(settings, standup), QString());
        settings.appRecognitionRules = {{QStringLiteral("zulip"), std::nullopt, standup}};
        QCOMPARE(writingProfileDeletionNotice(settings, standup),
                 QStringLiteral("1 application rule uses this profile and will lose it."));
        settings.appRecognitionRules.append({QStringLiteral("mattermost"), AppCategory::Browser, standup});
        settings.refinement.defaultWritingProfile = standup;
        QCOMPARE(writingProfileDeletionNotice(settings, standup),
                 QStringLiteral("2 application rules use this profile and will lose it. "
                                "The fallback profile will become Other."));
    }

    void profileChoicesOfferTheCustomTonesAndLevels()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        AppSettings settings;
        settings.refinement.customTones = {
            {QStringLiteral("custom_terse"), QStringLiteral("Terse"), QStringLiteral("Short.")}};
        settings.refinement.customCleanupLevels = {{QStringLiteral("custom_notes"), QStringLiteral("Notes"),
                                                    QStringLiteral("balanced"), QString()}};
        const auto ids = [](const QList<RowOption> &options) {
            QStringList ids;
            for (const RowOption &option : options) {
                ids.append(option.id + QLatin1Char('=') + option.label);
            }
            return ids;
        };
        const QList<CollectionColumn> &columns =
            schema.row(QStringLiteral("writingProfileBehavior"))->collection.columns;
        QCOMPARE(ids(columns.at(1).options(settings)),
                 (QStringList{QStringLiteral("none=None"), QStringLiteral("light_cleanup=Light"),
                              QStringLiteral("balanced=Medium"), QStringLiteral("strong_polish=High"),
                              QStringLiteral("custom_notes=Notes")}));
        QCOMPARE(ids(columns.at(2).options(settings)),
                 (QStringList{QStringLiteral("none=No tone"), QStringLiteral("formal=Formal"),
                              QStringLiteral("casual=Casual"), QStringLiteral("very_casual=Very casual"),
                              QStringLiteral("excited=Excited"), QStringLiteral("gen_z=Gen Z"),
                              QStringLiteral("custom_terse=Terse")}));
        // Each choice says what it does, for the profile dialog to show.
        QCOMPARE(columns.at(1).options(settings).last().help, QStringLiteral("Medium, plus your own instructions."));
        QCOMPARE(columns.at(2).options(settings).at(1).help, QStringLiteral("Professional and polished."));
        QCOMPARE(columns.at(2).options(settings).last().help, QStringLiteral("Short."));
    }

    // A profile's row says what it does and where Speecher uses it.
    void aProfileSummarySaysWhatItDoesAndWhereItApplies()
    {
        const ProviderLabels labels = providerLabels(chainContext());
        const auto writingProfileSummary = [&labels](const AppSettings &settings, const QString &id) {
            return speecher::writingProfileSummary(settings, id, labels);
        };
        AppSettings settings;
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("email")),
                 QStringLiteral("Medium cleanup, no tone. Used in Thunderbird, KMail and 1 more."));
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("other")),
                 QStringLiteral("Medium cleanup, no tone. Used when no other profile matches."));
        settings.appRecognitionRules = {{QStringLiteral("gmail"), std::nullopt, QStringLiteral("email")}};
        settings.refinement.writingProfiles = {{QStringLiteral("email"), QStringLiteral("strong_polish"),
                                                QStringLiteral("formal"), QStringLiteral("Sign off as Ann."),
                                                QString()},
                                               {QStringLiteral("custom_notes"), QStringLiteral("none"),
                                                QStringLiteral("none"), QString(), QStringLiteral("Notes")}};
        settings.refinement.defaultWritingProfile = QStringLiteral("custom_notes");
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("email")),
                 QStringLiteral("High cleanup, Formal tone. Has its own instructions. "
                                "Used in Gmail, Thunderbird and 2 more."));
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("custom_notes")),
                 QStringLiteral("No cleanup. Used when no other profile matches."));
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("other")), QStringLiteral("Medium cleanup, no tone."));

        // Its own services follow the tone; a profile that isn't refined
        // names only its speech service.
        settings.refinement.writingProfiles[0].refinementProvider = QStringLiteral("anthropic");
        settings.refinement.writingProfiles[0].refinementModel = QStringLiteral("claude-opus-5-5");
        settings.refinement.writingProfiles[1].speechProvider = QStringLiteral("local");
        settings.refinement.writingProfiles[1].speechModel = QStringLiteral("moonshine-small");
        settings.refinement.writingProfiles[1].refinementProvider = QStringLiteral("openai");
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("email")),
                 QStringLiteral("High cleanup, Formal tone, Anthropic Claude Opus 5.5. Has its own instructions. "
                                "Used in Gmail, Thunderbird and 2 more."));
        QCOMPARE(writingProfileSummary(settings, QStringLiteral("custom_notes")),
                 QStringLiteral("No cleanup, Local Model Moonshine Small. Used when no other profile matches."));
    }

    // The profile dialog's service rows: Default names the page's choice,
    // a Model row appears for a provider whose model a profile picks, a
    // model goes only with that provider, and a Local Model that can't hear
    // the Spoken Language says what runs instead.
    void aProfilePicksItsOwnServicesInItsDialog()
    {
        SchemaContext context = chainContext();
        context.liveFacts = [] {
            LiveFacts facts;
            facts.downloadedModels = {QStringLiteral("parakeet"), QStringLiteral("moonshine-small")};
            return facts;
        };
        const CollectionDescriptor grid =
            buildSettingsSchema(context).row(QStringLiteral("writingProfileBehavior"))->collection;
        const auto column = [&grid](const QString &id) {
            return *std::find_if(grid.columns.cbegin(), grid.columns.cend(),
                                 [&id](const CollectionColumn &column) { return column.id == id; });
        };
        const auto labelled = [](const QList<RowOption> &options) {
            QStringList ids;
            for (const RowOption &option : options) {
                ids.append(option.id + QLatin1Char('=') + option.label);
            }
            return ids;
        };
        AppSettings settings;
        settings.speech.language = QStringLiteral("ja");
        settings.refinement.providerId = QStringLiteral("none");

        const CollectionColumn speech = column(QStringLiteral("profileSpeech"));
        QCOMPARE(labelled(speech.options(settings)),
                 (QStringList{QStringLiteral("=Default (Claude Voice)"), QStringLiteral("claude=Claude Voice"),
                              QStringLiteral("codex=ChatGPT Codex"), QStringLiteral("local=Local Model"),
                              QStringLiteral("endpoint=Custom Endpoint")}));
        QCOMPARE(speech.options(settings).first().help, QStringLiteral("Set under Dictation, with its fallbacks."));
        QCOMPARE(labelled(column(QStringLiteral("profileRefinement")).options(settings)).first(),
                 QStringLiteral("=Default (None)"));

        QVariantMap record = grid.records(settings).at(3);
        const CollectionColumn speechModel = column(QStringLiteral("profileSpeechModel"));
        QVERIFY(speechModel.recordOptions(settings, record).isEmpty());
        record.insert(QStringLiteral("profileSpeech"), QStringLiteral("local"));
        record.insert(QStringLiteral("profileSpeechModel"), QStringLiteral("parakeet"));
        QCOMPARE(labelled(speechModel.recordOptions(settings, record)),
                 (QStringList{QStringLiteral("=Default (Parakeet 0.6B)"), QStringLiteral("parakeet=Parakeet 0.6B"),
                              QStringLiteral("moonshine-small=Moonshine Small")}));
        const FieldNote caution = speechModel.recordNote(settings, record);
        QVERIFY(caution.caution);
        QCOMPARE(caution.text, QStringLiteral("Parakeet 0.6B doesn't listen for Japanese, your Spoken Language, so "
                                              "this profile uses Claude Voice."));
        QCOMPARE(speech.recordNote(settings, record).text, QString());

        const CollectionColumn refinementModel = column(QStringLiteral("profileRefinementModel"));
        record.insert(QStringLiteral("profileRefinement"), QStringLiteral("anthropic"));
        record.insert(QStringLiteral("profileRefinementModel"), QStringLiteral("claude-sonnet-5-5"));
        QCOMPARE(labelled(refinementModel.recordOptions(settings, record)).mid(0, 2),
                 (QStringList{QStringLiteral("=Default (Claude Opus 5.5)"),
                              QStringLiteral("claude-opus-5-5=Claude Opus 5.5")}));
        QCOMPARE(refinementModel.recordNote(settings, record).text,
                 QStringLiteral("Thinking and speed are Anthropic's settings under Refinement."));
        record.insert(QStringLiteral("cleanup"), QStringLiteral("none"));
        QCOMPARE(column(QStringLiteral("profileRefinement")).recordNote(settings, record).text,
                 QStringLiteral("Cleanup None skips refinement for this profile."));

        // Haiku gets the caution its card under Refinement gives it.
        record.insert(QStringLiteral("profileRefinementModel"), QStringLiteral("claude-haiku-4-5"));
        const FieldNote haiku = refinementModel.recordNote(settings, record);
        QVERIFY(haiku.caution);
        QCOMPARE(haiku.text, QStringLiteral("Haiku may treat transcript as instructions."));
        record.insert(QStringLiteral("profileRefinementModel"), QStringLiteral("claude-sonnet-5-5"));

        // An OpenAI model a profile picks is kept, as an Anthropic one is.
        QVariantMap openAiModel = record;
        openAiModel.insert(QStringLiteral("profileRefinement"), QStringLiteral("openai"));
        openAiModel.insert(QStringLiteral("profileRefinementModel"), QStringLiteral("gpt-5.5"));
        QList<QVariantMap> picked = grid.records(settings);
        picked[3] = openAiModel;
        AppSettings withOpenAi = settings;
        grid.apply(withOpenAi, picked);
        QCOMPARE(writingProfileSettingsFor(withOpenAi.refinement.writingProfiles, WritingProfile::AiCoding).refinementModel,
                 QStringLiteral("gpt-5.5"));

        // A model left from another provider goes with the provider change:
        // OpenAI doesn't offer the Claude model, and saving drops it.
        QVariantMap toOpenAi = record;
        toOpenAi.insert(QStringLiteral("profileRefinement"), QStringLiteral("openai"));
        QVERIFY(!ids(refinementModel.recordOptions(settings, toOpenAi)).contains(QStringLiteral("claude-sonnet-5-5")));
        QList<QVariantMap> switched = grid.records(settings);
        switched[3] = toOpenAi;
        AppSettings openAi = settings;
        grid.apply(openAi, switched);
        QCOMPARE(writingProfileSettingsFor(openAi.refinement.writingProfiles, WritingProfile::AiCoding).refinementModel,
                 QString());

        record.insert(QStringLiteral("profileSpeech"), QStringLiteral("codex"));
        QList<QVariantMap> records = grid.records(settings);
        records[3] = record;
        grid.apply(settings, records);
        const WritingProfileSettings saved =
            writingProfileSettingsFor(settings.refinement.writingProfiles, WritingProfile::AiCoding);
        QCOMPARE(saved.speechProvider, QStringLiteral("codex"));
        QCOMPARE(saved.speechModel, QString());
        QCOMPARE(saved.refinementProvider, QStringLiteral("anthropic"));
        QCOMPARE(saved.refinementModel, QStringLiteral("claude-sonnet-5-5"));
    }

    // A model no list offers, such as one a later release stopped
    // suggesting, survives saving any profile until its own is changed.
    void aSavedModelNoListOffersIsKept()
    {
        const CollectionDescriptor grid =
            buildSettingsSchema(chainContext()).row(QStringLiteral("writingProfileBehavior"))->collection;
        const CollectionColumn model = *std::find_if(grid.columns.cbegin(), grid.columns.cend(),
                                                     [](const CollectionColumn &column) {
                                                         return column.id == QStringLiteral("profileRefinementModel");
                                                     });
        AppSettings settings;
        settings.refinement.writingProfiles[1].refinementProvider = QStringLiteral("anthropic");
        settings.refinement.writingProfiles[1].refinementModel = QStringLiteral("claude-sonnet-4-6");
        QVERIFY(ids(model.recordOptions(settings, grid.records(settings).at(1))).contains(QStringLiteral("claude-sonnet-4-6")));
        AppSettings saved = settings;
        grid.apply(saved, grid.records(settings));
        QCOMPARE(writingProfileSettingsFor(saved.refinement.writingProfiles, WritingProfile::Email).refinementModel,
                 QStringLiteral("claude-sonnet-4-6"));

        QList<QVariantMap> switched = grid.records(settings);
        switched[1].insert(QStringLiteral("profileRefinement"), QStringLiteral("openai"));
        QVERIFY(!ids(model.recordOptions(settings, switched.at(1))).contains(QStringLiteral("claude-sonnet-4-6")));
        grid.apply(settings, switched);
        QCOMPARE(writingProfileSettingsFor(settings.refinement.writingProfiles, WritingProfile::Email).refinementModel,
                 QString());
    }

    // A service a profile names that this build lacks stays in its dialog,
    // disabled, so an unrelated edit keeps it.
    void aProfileKeepsAServiceThisBuildLacks()
    {
        const CollectionDescriptor grid =
            buildSettingsSchema(fakeContext()).row(QStringLiteral("writingProfileBehavior"))->collection;
        const CollectionColumn speech = *std::find_if(grid.columns.cbegin(), grid.columns.cend(),
                                                      [](const CollectionColumn &column) {
                                                          return column.id == QStringLiteral("profileSpeech");
                                                      });
        AppSettings settings;
        settings.refinement.writingProfiles[3].speechProvider = QStringLiteral("local");
        const QList<RowOption> options = speech.recordOptions(settings, grid.records(settings).at(3));
        QCOMPARE(options.last().id, QStringLiteral("local"));
        QCOMPARE(options.last().label, QStringLiteral("Provider not in this build"));
        QVERIFY(!options.last().enabled);
        grid.apply(settings, grid.records(settings));
        QCOMPARE(writingProfileSettingsFor(settings.refinement.writingProfiles, WritingProfile::AiCoding).speechProvider,
                 QStringLiteral("local"));
    }

    // A profile can pick a speech service and refine for itself, so its list
    // stays open while the Refinement page says None, and a provider only a
    // profile picks still shows its card.
    void profilesStayEditableAndTheirProvidersShowTheirCards()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("none");
        const SettingsRow *profiles = schema.row(QStringLiteral("writingProfileBehavior"));
        QVERIFY(!profiles->enabled || profiles->enabled(settings, Capabilities{}));
        const SettingsRow *instructions = schema.row(QStringLiteral("additionalInstructions"));
        QVERIFY(!instructions->enabled(settings, Capabilities{}));
        const SettingsRow *anthropicModel = schema.row(QStringLiteral("anthropicModel"));
        QVERIFY(!anthropicModel->visible(settings, Capabilities{}));

        settings.refinement.writingProfiles[1].refinementProvider = QStringLiteral("anthropic");
        QVERIFY(instructions->enabled(settings, Capabilities{}));
        QVERIFY(anthropicModel->visible(settings, Capabilities{}));
    }

    // A row no pane shows is a setting nobody can reach, and a row two panes
    // show is two places to look; either way the platforms drift apart.
    void everySchemaRowIsOnExactlyOnePane()
    {
        for (const bool everything : {false, true}) {
            SchemaContext context = fakeContext();
            if (everything) {
                context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
                context.virtualKeyboardSetup = true;
                context.lastSeenVersion = QStringLiteral("0.0.0");
                context.currentVersion = QStringLiteral("0.1.0");
            }
            const SettingsSchema schema = buildSettingsSchema(context);
            QStringList placed;
            for (const SettingsPane &pane : schema.panes) {
                for (const SettingsPaneGroup &group : pane.groups) {
                    if (pane.id != QStringLiteral("whatsNew")) {
                        placed += group.rows;
                    }
                }
            }
            for (const SettingsSubpage &subpage : schema.subpages) {
                for (const SettingsPaneGroup &group : subpage.groups) {
                    placed += group.rows;
                }
            }
            for (const SettingsPage &page : schema.pages) {
                if (page.id == QStringLiteral("whatsNew")) {
                    continue;
                }
                for (const SettingsSection &section : page.sections) {
                    for (const SettingsRow &row : section.rows) {
                        QVERIFY2(placed.count(row.id) == 1,
                                 qPrintable(QStringLiteral("%1 is on %2 panes").arg(row.id).arg(placed.count(row.id))));
                    }
                }
            }
        }
    }

    // The user-approved arrangement, which every front end's sidebar shows.
    void panesFollowTheAgreedArrangement()
    {
        SchemaContext context = fakeContext();
        context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
        const SettingsSchema schema = buildSettingsSchema(context);
        QCOMPARE(schema.sidebarGroups,
                 (QList<SidebarGroup>{
                     {QString(),
                      {QStringLiteral("home"), QStringLiteral("transcribe"), QStringLiteral("general"),
                       QStringLiteral("accounts")}},
                     {QStringLiteral("Speech"), {QStringLiteral("dictation"), QStringLiteral("localModels")}},
                     {QStringLiteral("Text"),
                      {QStringLiteral("refinement"), QStringLiteral("writingProfiles"),
                       QStringLiteral("vocabulary"), QStringLiteral("output")}}}));
        const auto paneOf = [&schema](const QString &rowId) {
            for (const SettingsPane &pane : schema.panes) {
                for (const SettingsPaneGroup &group : pane.groups) {
                    if (group.rows.contains(rowId)) {
                        return pane.id;
                    }
                }
            }
            return QString();
        };
        for (const QString &row : {QStringLiteral("openAiAuthMode"), QStringLiteral("anthropicAuthMode"),
                                   QStringLiteral("cliproxyBaseUrl")}) {
            QCOMPARE(paneOf(row), QStringLiteral("accounts"));
        }
        for (const QString &row : {QStringLiteral("openAiModel"), QStringLiteral("openAiEffort"),
                                   QStringLiteral("anthropicFastMode")}) {
            QCOMPARE(paneOf(row), QStringLiteral("refinement"));
        }
        // The recorder and Shortcut behavior lead Dictation; the app rules are
        // sections of Output.
        QCOMPARE(paneOf(QStringLiteral("globalShortcut")), QStringLiteral("dictation"));
        QCOMPARE(paneOf(QStringLiteral("activationMode")), QStringLiteral("dictation"));
        QCOMPARE(schema.pane(QStringLiteral("dictation"))->groups.first().rows,
                 (QStringList{QStringLiteral("globalShortcut"), QStringLiteral("activationMode"),
                              QStringLiteral("cancelShortcut"), QStringLiteral("pauseShortcut")}));
        QCOMPARE(paneOf(QStringLiteral("appRecognitionRules")), QStringLiteral("output"));
        QCOMPARE(paneOf(QStringLiteral("applicationPasteRules")), QStringLiteral("output"));
        QStringList outputSections;
        for (const SettingsPaneGroup &group : schema.pane(QStringLiteral("output"))->groups) {
            outputSections.append(group.title);
        }
        QCOMPARE(outputSections, (QStringList{QStringLiteral("Delivery"), QStringLiteral("Paste rules"),
                                              QStringLiteral("App-specific paste rules"),
                                              QStringLiteral("Application recognition")}));
        QCOMPARE(schema.pane(QStringLiteral("output"))->layout, PaneLayout::Sections);
        QVERIFY(!schema.pane(QStringLiteral("shortcut")));
        QVERIFY(!schema.pane(QStringLiteral("apps")));
        QStringList views;
        for (const SettingsPaneGroup &group : schema.pane(QStringLiteral("vocabulary"))->groups) {
            views.append(group.title);
        }
        QCOMPARE(views, (QStringList{QStringLiteral("Terms"), QStringLiteral("Learned corrections"),
                                     QStringLiteral("Replacements & snippets")}));
    }

    void everyPageIdResolves()
    {
        SchemaContext context = fakeContext();
        context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
        const SettingsSchema schema = buildSettingsSchema(context);
        for (const SettingsPane &pane : schema.panes) {
            QCOMPARE(resolvePage(schema, pane.id).pane, pane.id);
            QCOMPARE(resolvePage(schema, pane.id.toLower()).pane, pane.id);
            QCOMPARE(paneTitle(pane.id), pane.title);
            QVERIFY2(!pane.groups.isEmpty() || pane.layout == PaneLayout::Home
                         || pane.layout == PaneLayout::Transcribe,
                     qPrintable(pane.id));
            for (const SettingsPaneGroup &group : pane.groups) {
                QVERIFY2(!group.title.isEmpty() || pane.id == QStringLiteral("whatsNew"), qPrintable(pane.id));
                if (pane.layout == PaneLayout::Alternatives) {
                    const PageId view = resolvePage(schema, pane.id + QLatin1Char(':') + group.view);
                    QCOMPARE(view.pane, pane.id);
                    QCOMPARE(view.view, group.view);
                }
            }
        }
        QCOMPARE(resolvePage(schema, QStringLiteral("vocabulary")).view, QStringLiteral("terms"));
        // Pages once of their own land where their settings went.
        QCOMPARE(resolvePage(schema, QStringLiteral("shortcut")).pane, QStringLiteral("dictation"));
        QCOMPARE(resolvePage(schema, QStringLiteral("apps")).pane, QStringLiteral("output"));
        QCOMPARE(resolvePage(schema, QStringLiteral("apps:recognition")).pane, QStringLiteral("output"));
        QCOMPARE(resolvePage(schema, QStringLiteral("Apps:PasteRules")).pane, QStringLiteral("output"));

        QTest::ignoreMessage(QtWarningMsg, "no settings page nothing - showing Home");
        QCOMPARE(resolvePage(schema, QStringLiteral("nothing")).pane, QStringLiteral("home"));
        // A view the pane does not have is as unknown as a pane.
        QTest::ignoreMessage(QtWarningMsg, "no settings page vocabulary:missing - showing Home");
        QCOMPARE(resolvePage(schema, QStringLiteral("vocabulary:missing")).pane, QStringLiteral("home"));
        QTest::ignoreMessage(QtWarningMsg, "no settings page general:missing - showing Home");
        QCOMPARE(resolvePage(schema, QStringLiteral("general:missing")).pane, QStringLiteral("home"));

    }

    void searchLooksThroughTitlesRowsAndHelpButNotWhatsNew()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const auto searchPanes = [&schema](const QString &query) {
            return speecher::searchPanes(schema, query, AppSettings{}, Capabilities{});
        };
        QCOMPARE(searchPanes(QStringLiteral("keep before speech")),
                 QStringList{QStringLiteral("dictation")});
        QCOMPARE(searchPanes(QStringLiteral("Replacements")),
                 QStringList{QStringLiteral("vocabulary")});
        QVERIFY(searchPanes(QStringLiteral("Sign-in")).contains(QStringLiteral("accounts")));
        QCOMPARE(searchPanes(QStringLiteral("What's New")), QStringList{QStringLiteral("general")});
        QVERIFY(searchPanes(QStringLiteral("Try the new settings")).isEmpty());
        // A hidden row is not found: the CLI Proxy API rows show only while a
        // provider signs in through it.
        QVERIFY(searchPanes(QStringLiteral("Account directory")).isEmpty());
        AppSettings throughProxy;
        throughProxy.refinement.openAiAuthMode = QStringLiteral("cliproxy");
        QCOMPARE(speecher::searchPanes(schema, QStringLiteral("Account directory"), throughProxy, Capabilities{}),
                 QStringList{QStringLiteral("accounts")});

        // Each match names the rows that mention the query, so a front end can
        // open the pane at the first of them.
        const QList<SearchMatch> matches =
            searchSettings(schema, QStringLiteral("keep before speech"), AppSettings{}, Capabilities{});
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().rows, QStringList{QStringLiteral("preRollMs")});
    }

    // Help and error text that sends someone to a page names it through
    // paneTitle or paneTitleForRow; this pins that the names are the panes'.
    void pageNamesInTextArePaneTitles()
    {
        QCOMPARE(paneTitle(QStringLiteral("localModels")), QStringLiteral("Local models"));
        QCOMPARE(paneTitleForRow(QStringLiteral("openAiModel")), QStringLiteral("Refinement"));
        QCOMPARE(paneTitleForRow(QStringLiteral("openAiCliproxyAccount")), QStringLiteral("Accounts"));
        QCOMPARE(refinementModelHint(), QStringLiteral("Set in Refinement settings"));

        SchemaContext context = fakeContext();
        context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
        const SettingsSchema schema = buildSettingsSchema(context);
        const QStringList titles = [&schema] {
            QStringList titles;
            for (const SettingsPane &pane : schema.panes) {
                titles.append(pane.title);
            }
            return titles;
        }();
        // Every "under X" or "the X page" the schema itself says names a pane.
        static const QRegularExpression pageName(
            QStringLiteral("(?:under|on the) ([A-Z][a-z]+(?: [a-z]+)?)(?: page|\\.| to)"));
        int named = 0;
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                QStringList texts{section.help};
                for (const SettingsRow &row : section.rows) {
                    texts << row.help << row.disabledHelp << row.tooltip;
                }
                for (const QString &text : std::as_const(texts)) {
                    auto matches = pageName.globalMatch(text);
                    while (matches.hasNext()) {
                        const QString name = matches.next().captured(1);
                        ++named;
                        QVERIFY2(titles.contains(name), qPrintable(text));
                    }
                }
            }
        }
        QVERIFY(named >= 2);
    }

    // A provider's own rows are a card named after it, shown while the
    // provider is anywhere in the chain.
    void providerRowsAreCardsShownWhileTheProviderIsInTheChain()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const auto cardRows = [&schema](const QString &pane, const QString &title) {
            for (const SettingsPaneGroup &group : schema.pane(pane)->groups) {
                if (group.title == title) {
                    return group.rows;
                }
            }
            return QStringList();
        };
        QCOMPARE(cardRows(QStringLiteral("dictation"), QStringLiteral("Transcription")),
                 QStringList({QStringLiteral("speechProvider"), QStringLiteral("speechRating"),
                              QStringLiteral("speechModels"), QStringLiteral("speechFallbacks"),
                              QStringLiteral("spokenLanguage"), QStringLiteral("spokenLanguageCaution"),
                              QStringLiteral("codexFinalRetranscribe")}));
        QCOMPARE(cardRows(QStringLiteral("dictation"), QStringLiteral("Local Model")),
                 QStringList({QStringLiteral("speechLocalModel"), QStringLiteral("speechLocalModelDownload")}));
        QCOMPARE(cardRows(QStringLiteral("dictation"), QStringLiteral("Custom Endpoint")),
                 QStringList({QStringLiteral("speechEndpointUrl"), QStringLiteral("speechEndpointPath"),
                              QStringLiteral("speechEndpointApiKey"), QStringLiteral("speechEndpointModel"),
                              QStringLiteral("speechEndpointTest")}));
        QCOMPARE(cardRows(QStringLiteral("refinement"), QStringLiteral("Provider")),
                 QStringList({QStringLiteral("refinementProvider"), QStringLiteral("refinementRating"),
                              QStringLiteral("refinementFallbacks")}));
        QCOMPARE(cardRows(QStringLiteral("refinement"), QStringLiteral("Anthropic")),
                 QStringList({QStringLiteral("anthropicModel"), QStringLiteral("anthropicModelCaution"),
                              QStringLiteral("anthropicEffort"), QStringLiteral("anthropicFastMode")}));
        QCOMPARE(cardRows(QStringLiteral("refinement"), QStringLiteral("Local Runner")),
                 QStringList({QStringLiteral("localRunner"), QStringLiteral("localRunnerModel"),
                              QStringLiteral("localRunnerDetect")}));
        QCOMPARE(cardRows(QStringLiteral("refinement"), QStringLiteral("Custom Endpoint")),
                 QStringList({QStringLiteral("refinementEndpointServer"), QStringLiteral("refinementEndpointFormat"),
                              QStringLiteral("refinementEndpointUrl"), QStringLiteral("refinementEndpointApiKey"),
                              QStringLiteral("refinementEndpointModel"), QStringLiteral("refinementEndpointTest")}));

        const SettingsPage &audio = schema.page(QStringLiteral("audio"));
        const SettingsPage &refinement = schema.page(QStringLiteral("refinement"));
        AppSettings settings;
        const Capabilities capabilities;
        const SettingsRow &speechUrl = rowById(audio, QStringLiteral("speechEndpointUrl"));
        const SettingsRow &retranscribe = rowById(audio, QStringLiteral("codexFinalRetranscribe"));
        const SettingsRow &runner = rowById(refinement, QStringLiteral("localRunner"));
        const SettingsRow &refinementUrl = rowById(refinement, QStringLiteral("refinementEndpointUrl"));
        const SettingsRow &anthropicModel = rowById(refinement, QStringLiteral("anthropicModel"));
        QVERIFY(!speechUrl.visible(settings, capabilities));
        QVERIFY(!retranscribe.visible(settings, capabilities));
        QVERIFY(!runner.visible(settings, capabilities));
        QVERIFY(!anthropicModel.visible(settings, capabilities));
        settings.speech.providerId = QStringLiteral("endpoint");
        settings.refinement.providerId = QStringLiteral("local");
        QVERIFY(speechUrl.visible(settings, capabilities));
        QVERIFY(runner.visible(settings, capabilities));
        QVERIFY(!refinementUrl.visible(settings, capabilities));
        // As fallbacks, too.
        settings.speech.fallbackProviderIds = {QStringLiteral("codex")};
        settings.refinement.fallbackProviderIds = {QStringLiteral("endpoint"), QStringLiteral("anthropic")};
        QVERIFY(retranscribe.visible(settings, capabilities));
        QVERIFY(refinementUrl.visible(settings, capabilities));
        QVERIFY(anthropicModel.visible(settings, capabilities));
        // None runs nothing, whatever the fallbacks say.
        settings.refinement.providerId = QStringLiteral("none");
        QVERIFY(!refinementUrl.visible(settings, capabilities));

        QVERIFY(rowById(audio, QStringLiteral("speechEndpointApiKey")).secret);
        QVERIFY(rowById(refinement, QStringLiteral("refinementEndpointApiKey")).secret);
        rowById(audio, QStringLiteral("speechEndpointPath")).apply(settings, QStringLiteral(" /inference "));
        rowById(refinement, QStringLiteral("refinementEndpointModel")).apply(settings, QStringLiteral("gemma4:e4b"));
        rowById(refinement, QStringLiteral("localRunnerModel")).apply(settings, QStringLiteral("lfm"));
        QCOMPARE(settings.speech.endpoint.path, QStringLiteral("/inference"));
        QCOMPARE(settings.refinement.endpoint.model, QStringLiteral("gemma4:e4b"));
        QCOMPARE(settings.refinement.localRunner.model, QStringLiteral("lfm"));
    }

    void theFallbacksRowOpensASubpageAndSaysWhatItHolds()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        const SettingsRow *speech = schema.row(QStringLiteral("speechFallbacks"));
        const SettingsRow *refinement = schema.row(QStringLiteral("refinementFallbacks"));
        QCOMPARE(speech->kind, RowKind::Action);
        QCOMPARE(speech->targetPage, QStringLiteral("dictation:fallbacks"));
        QCOMPARE(refinement->targetPage, QStringLiteral("refinement:fallbacks"));

        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        QCOMPARE(speech->helpValue(settings), QStringLiteral("None. Dictation stops if ChatGPT Codex is unavailable."));
        QCOMPARE(refinement->helpValue(settings),
                 QStringLiteral("None. Your words are pasted as spoken if OpenAI is unavailable."));
        settings.speech.fallbackProviderIds = {QStringLiteral("endpoint"), QStringLiteral("local")};
        settings.speech.endpoint.baseUrl = QStringLiteral("http://localhost:8080");
        QCOMPARE(speech->helpValue(settings),
                 QStringLiteral("Custom Endpoint, then Local Model. No model downloaded, so it can't stand in yet."));
        QVERIFY(refinement->visible(settings, Capabilities{}));
        settings.refinement.providerId = QStringLiteral("none");
        QVERIFY(!refinement->visible(settings, Capabilities{}));

        // The subpage is no pane of its own: the sidebar keeps its parent.
        const PageId page = resolvePage(schema, QStringLiteral("Dictation:Fallbacks"));
        QCOMPARE(page.pane, QStringLiteral("dictation"));
        QCOMPARE(page.subpage, QStringLiteral("dictation:fallbacks"));
        QVERIFY(resolvePage(schema, QStringLiteral("dictation")).subpage.isEmpty());
        const SettingsSubpage *subpage = schema.subpage(QStringLiteral("refinement:fallbacks"));
        QCOMPARE(subpage->parent, QStringLiteral("refinement"));
        QCOMPARE(subpage->title, QStringLiteral("Fallbacks"));
        QCOMPARE(subpage->groups.first().rows, QStringList{QStringLiteral("refinementFallbackList")});
        QVERIFY(!schema.pane(QStringLiteral("dictation:fallbacks")));
    }

    void aNewPrimaryLeavesTheFallbacksThroughTheRows()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        AppSettings settings;
        schema.row(QStringLiteral("speechFallbackList"))
            ->apply(settings, QStringList{QStringLiteral("codex"), QStringLiteral("claude"), QStringLiteral("local"),
                                          QStringLiteral("codex")});
        QCOMPARE(settings.speech.fallbackProviderIds, (QStringList{QStringLiteral("codex"), QStringLiteral("local")}));
        schema.row(QStringLiteral("speechProvider"))->apply(settings, QStringLiteral("codex"));
        QCOMPARE(settings.speech.fallbackProviderIds, QStringList{QStringLiteral("local")});

        schema.row(QStringLiteral("refinementFallbackList"))->apply(settings, QStringList{QStringLiteral("local")});
        schema.row(QStringLiteral("refinementProvider"))->apply(settings, QStringLiteral("none"));
        QVERIFY(settings.refinement.fallbackProviderIds.isEmpty());
        QCOMPARE(schema.row(QStringLiteral("refinementFallbackList"))->value(settings).toStringList(), QStringList());
    }

    void theFallbackListOffersWhatIsLeftUntilTheChainIsFull()
    {
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("local")};
        settings.speech.local.modelId = QStringLiteral("parakeet");
        LiveFacts facts;
        facts.downloadedModels = {QStringLiteral("parakeet")};

        FallbackListPresentation list =
            fallbackListPresentation(ProviderRole::Speech, settings, facts, speechChoices(), FallbackSurface::Settings);
        QCOMPARE(list.heading, QStringLiteral("If ChatGPT Codex is unavailable"));
        QVERIFY(list.footer.isEmpty());
        QCOMPARE(list.items.size(), 1);
        QCOMPARE(list.items.first().label, QStringLiteral("Local Model"));
        QVERIFY(!list.items.first().canMoveUp && !list.items.first().canMoveDown);
        QVERIFY(list.canAdd);
        QCOMPARE(ids(list.addChoices), (QStringList{QStringLiteral("claude"), QStringLiteral("endpoint")}));
        // Setup sets up no speech Custom Endpoint.
        QCOMPARE(ids(fallbackListPresentation(ProviderRole::Speech, settings, facts, speechChoices(),
                                              FallbackSurface::Setup)
                         .addChoices),
                 QStringList{QStringLiteral("claude")});

        settings.speech.fallbackProviderIds = withFallbackAdded(settings, ProviderRole::Speech, QStringLiteral("claude"));
        list = fallbackListPresentation(ProviderRole::Speech, settings, facts, speechChoices(), FallbackSurface::Settings);
        QCOMPARE(list.items.size(), 2);
        QVERIFY(!list.items[0].canMoveUp && list.items[0].canMoveDown);
        QVERIFY(list.items[1].canMoveUp && !list.items[1].canMoveDown);
        QVERIFY(!list.canAdd);

        // Refinement says what happens when none answers, and None has no list.
        settings.refinement.providerId = QStringLiteral("anthropic");
        list = fallbackListPresentation(ProviderRole::Refinement, settings, facts, refinementChoices(),
                                        FallbackSurface::Settings);
        QCOMPARE(list.footer, QStringLiteral("If none of these answers, your words are pasted as spoken."));
        QCOMPARE(ids(list.addChoices),
                 (QStringList{QStringLiteral("openai"), QStringLiteral("endpoint"), QStringLiteral("local")}));
        settings.refinement.providerId = QStringLiteral("none");
        list = fallbackListPresentation(ProviderRole::Refinement, settings, facts, refinementChoices(),
                                        FallbackSurface::Settings);
        QVERIFY(list.heading.isEmpty() && list.items.isEmpty() && !list.canAdd);
    }

    void fallbackEditsKeepTheChainNormalized()
    {
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("endpoint"), QStringLiteral("local")};
        QCOMPARE(withFallbackMoved(settings, ProviderRole::Speech, 1, -1),
                 (QStringList{QStringLiteral("local"), QStringLiteral("endpoint")}));
        // A move past either end changes nothing.
        QCOMPARE(withFallbackMoved(settings, ProviderRole::Speech, 1, 1), settings.speech.fallbackProviderIds);
        QCOMPARE(withFallbackRemoved(settings, ProviderRole::Speech, 0), QStringList{QStringLiteral("local")});
        // The chain is full, and the primary is never its own fallback.
        QCOMPARE(withFallbackAdded(settings, ProviderRole::Speech, QStringLiteral("claude")),
                 settings.speech.fallbackProviderIds);
        settings.speech.fallbackProviderIds.removeLast();
        QCOMPARE(withFallbackAdded(settings, ProviderRole::Speech, QStringLiteral("codex")),
                 QStringList{QStringLiteral("endpoint")});
        QCOMPARE(withFallbackAdded(settings, ProviderRole::Speech, QStringLiteral("claude")),
                 (QStringList{QStringLiteral("endpoint"), QStringLiteral("claude")}));
    }

    // A fallback's row says why it can't stand in, from cached facts alone,
    // and otherwise which turn it gets.
    void aFallbackSaysWhyItCantStandInRightNow()
    {
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.local.modelId = QStringLiteral("parakeet");
        settings.speech.language = QStringLiteral("en");
        settings.refinement.providerId = QStringLiteral("openai");
        LiveFacts facts;
        const auto speech = [&](const QString &id) {
            return fallbackProblem(ProviderRole::Speech, id, settings, facts);
        };
        const auto refinement = [&](const QString &id) {
            return fallbackProblem(ProviderRole::Refinement, id, settings, facts);
        };

        // Unknown reachability and an unread sign-in are not problems.
        QCOMPARE(speech(QStringLiteral("claude")), FallbackProblem::None);
        facts.signedIn.insert(QStringLiteral("claude"), false);
        QCOMPARE(speech(QStringLiteral("claude")), FallbackProblem::SignedOut);
        QCOMPARE(speech(QStringLiteral("local")), FallbackProblem::NoModel);
        QCOMPARE(speech(QStringLiteral("endpoint")), FallbackProblem::NoServer);
        facts.downloadedModels = {QStringLiteral("parakeet")};
        QCOMPARE(speech(QStringLiteral("local")), FallbackProblem::None);
        settings.speech.language = QStringLiteral("fr");
        QCOMPARE(speech(QStringLiteral("local")), FallbackProblem::SpokenLanguage);
        QVERIFY(fallbackSkipsSpokenLanguage(settings.speech, QStringLiteral("local")));
        QVERIFY(!fallbackSkipsSpokenLanguage(settings.speech, QStringLiteral("codex")));

        // Offline stops the cloud and a server out on the internet, not one
        // on this computer or its network.
        facts.reachability = Reachability::Offline;
        QCOMPARE(refinement(QStringLiteral("anthropic")), FallbackProblem::Offline);
        settings.speech.endpoint.baseUrl = QStringLiteral("http://192.168.1.20:8000");
        QCOMPARE(speech(QStringLiteral("endpoint")), FallbackProblem::None);
        settings.speech.endpoint.baseUrl = QStringLiteral("http://localhost:8080");
        QCOMPARE(speech(QStringLiteral("endpoint")), FallbackProblem::None);
        settings.speech.endpoint.baseUrl = QStringLiteral("https://api.example.com");
        QCOMPARE(speech(QStringLiteral("endpoint")), FallbackProblem::Offline);

        // A runner is missing only once a look has found it isn't running.
        settings.refinement.localRunner.runner = QStringLiteral("ollama");
        QCOMPARE(refinement(QStringLiteral("local")), FallbackProblem::None);
        facts.runnersChecked = true;
        QCOMPARE(refinement(QStringLiteral("local")), FallbackProblem::NoRunner);
        facts.runners = {{QStringLiteral("ollama"), QStringLiteral("Ollama 0.34.4")}};
        QCOMPARE(refinement(QStringLiteral("local")), FallbackProblem::None);

        settings.refinement.fallbackProviderIds = {QStringLiteral("local"), QStringLiteral("anthropic")};
        const FallbackListPresentation list = fallbackListPresentation(
            ProviderRole::Refinement, settings, facts, refinementChoices(), FallbackSurface::Settings);
        QCOMPARE(list.items[0].tone, StatusTone::Normal);
        QCOMPARE(list.items[0].status, QStringLiteral("Used when OpenAI is unavailable."));
        QCOMPARE(list.items[1].tone, StatusTone::Negative);
    }

    // A primary that can't work right now says why on its own row, in the
    // negative tone, and which fallback takes over, from cached facts only.
    void aPrimaryThatCantWorkSaysWhatTakesOver()
    {
        LiveFacts facts;
        facts.reachability = Reachability::Offline;
        SchemaContext context = chainContext();
        context.liveFacts = [&facts] { return facts; };
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow *speech = schema.row(QStringLiteral("speechProvider"));
        const SettingsRow *refinement = schema.row(QStringLiteral("refinementProvider"));
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("endpoint")};
        settings.speech.endpoint.baseUrl = QStringLiteral("http://localhost:8080");
        settings.refinement.providerId = QStringLiteral("openai");
        QCOMPARE(speech->helpValue(settings),
                 QStringLiteral("Can't reach ChatGPT right now. Dictation starts with Custom Endpoint."));
        QCOMPARE(speech->helpTone(settings), StatusTone::Negative);
        QCOMPARE(refinement->helpValue(settings),
                 QStringLiteral("Can't reach OpenAI right now. Your words are pasted as spoken."));
        QCOMPARE(refinement->helpTone(settings), StatusTone::Negative);

        facts.reachability = Reachability::Unknown;
        QCOMPARE(speech->helpTone(settings), StatusTone::Normal);
        QVERIFY(!speech->helpValue(settings).contains(QStringLiteral("Can't reach")));
        QCOMPARE(refinement->helpValue(settings), QStringLiteral("The service that cleans up your text."));
    }

    // `speecher providers` judges every provider from the facts its rows
    // read, says unknown for what nobody has checked, and words a problem as
    // the primary's row does.
    void aProviderReportAgreesWithItsRow()
    {
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.local.modelId = QStringLiteral("parakeet");
        settings.speech.language = QStringLiteral("en");
        settings.refinement.providerId = QStringLiteral("openai");
        LiveFacts facts;
        const auto report = [&](ProviderRole role, const QString &id) { return reportOf(role, id, settings, facts); };

        const QList<ProviderReport> reports = providerReports(settings, facts, speechChoices(), refinementChoices());
        QCOMPARE(reports.size(), 8);
        QCOMPARE(reports.first().label, QStringLiteral("Claude Voice"));
        QCOMPARE(reports.last().role, ProviderRole::Refinement);
        QCOMPARE(reports.last().label, QStringLiteral("Local Runner"));

        // A sign-in nobody has seen and a runner nobody looked for are unknown.
        ProviderReport codex = report(ProviderRole::Speech, QStringLiteral("codex"));
        QVERIFY(codex.configured);
        QVERIFY(codex.signsIn);
        QCOMPARE(codex.signedIn, std::nullopt);
        QCOMPARE(codex.usable, std::nullopt);
        QVERIFY(codex.problem.isEmpty());
        settings.refinement.localRunner.runner = QStringLiteral("ollama");
        QCOMPARE(report(ProviderRole::Refinement, QStringLiteral("local")).usable, std::nullopt);
        facts.runnersChecked = true;
        const ProviderReport runner = report(ProviderRole::Refinement, QStringLiteral("local"));
        QVERIFY(runner.configured);
        QCOMPARE(runner.usable, std::optional(false));
        QCOMPARE(runner.problem, QStringLiteral("Ollama isn't running."));

        // What settings must hold.
        const ProviderReport local = report(ProviderRole::Speech, QStringLiteral("local"));
        QVERIFY(!local.configured);
        QVERIFY(!local.signsIn);
        QCOMPARE(local.signedIn, std::nullopt);
        QCOMPARE(local.usable, std::optional(false));
        QCOMPARE(local.problem, QStringLiteral("No model downloaded."));
        QCOMPARE(report(ProviderRole::Refinement, QStringLiteral("endpoint")).problem,
                 QStringLiteral("No server URL is set."));
        facts.downloadedModels = {QStringLiteral("parakeet")};
        QCOMPARE(report(ProviderRole::Speech, QStringLiteral("local")).usable, std::optional(true));

        // A sign-in once seen, and the primary's row saying the same.
        facts.signedIn.insert(QStringLiteral("codex"), false);
        facts.signedIn.insert(QStringLiteral("claude"), true);
        codex = report(ProviderRole::Speech, QStringLiteral("codex"));
        QCOMPARE(codex.signedIn, std::optional(false));
        QCOMPARE(codex.usable, std::optional(false));
        QCOMPARE(codex.problem, QStringLiteral("Not signed in to ChatGPT."));
        QCOMPARE(primaryProviderStatus(ProviderRole::Speech, settings, facts, speechChoices()), codex.problem);
        QCOMPARE(report(ProviderRole::Speech, QStringLiteral("claude")).usable, std::optional(true));

        facts.reachability = Reachability::Offline;
        settings.speech.endpoint.baseUrl = QStringLiteral("https://api.example.com");
        const ProviderReport endpoint = report(ProviderRole::Speech, QStringLiteral("endpoint"));
        QVERIFY(endpoint.configured);
        QCOMPARE(endpoint.problem, QStringLiteral("Can't reach Custom Endpoint right now."));
    }

    // A primary's report gives the problem its row gives, less the row's word
    // on what happens instead.
    void aProviderReportWordsAProblemAsThePrimarysRow()
    {
        AppSettings settings;
        settings.speech.language = QStringLiteral("en");
        settings.speech.local.modelId = QStringLiteral("parakeet");
        LiveFacts facts;
        // The report's problem, then the row's.
        const auto speechProblems = [&](const QString &primary) {
            settings.speech.providerId = primary;
            return QStringList{reportOf(ProviderRole::Speech, primary, settings, facts).problem,
                               primaryProviderStatus(ProviderRole::Speech, settings, facts, speechChoices())};
        };
        QCOMPARE(speechProblems(QStringLiteral("endpoint")), QStringList(2, QStringLiteral("No server URL is set.")));
        QCOMPARE(speechProblems(QStringLiteral("local")), QStringList(2, QStringLiteral("No model downloaded.")));
        facts.reachability = Reachability::Offline;
        QCOMPARE(speechProblems(QStringLiteral("codex")),
                 QStringList(2, QStringLiteral("Can't reach ChatGPT right now.")));

        settings.refinement.providerId = QStringLiteral("local");
        facts.runnersChecked = true;
        const ProviderReport unchosen = reportOf(ProviderRole::Refinement, QStringLiteral("local"), settings, facts);
        QVERIFY(!unchosen.configured);
        QCOMPARE(unchosen.problem, QStringLiteral("No Ollama, LM Studio or llama-server is running."));
        settings.refinement.localRunner.runner = QStringLiteral("ollama");
        const ProviderReport runner = reportOf(ProviderRole::Refinement, QStringLiteral("local"), settings, facts);
        QVERIFY(runner.configured);
        QCOMPARE(runner.problem, QStringLiteral("Ollama isn't running."));
        QCOMPARE(primaryProviderStatus(ProviderRole::Refinement, settings, facts, refinementChoices()),
                 QStringLiteral("Ollama isn't running. Your words are pasted as spoken."));

        // The Spoken Language row's own words, where the primary's row says nothing.
        facts.downloadedModels = {QStringLiteral("parakeet")};
        settings.speech.providerId = QStringLiteral("local");
        settings.speech.language = QStringLiteral("ja");
        const ProviderReport local = reportOf(ProviderRole::Speech, QStringLiteral("local"), settings, facts);
        QCOMPARE(local.usable, std::optional(false));
        QCOMPARE(local.problem,
                 QStringLiteral("Parakeet 0.6B can't listen for Japanese. Choose another Spoken Language."));
        QCOMPARE(local.problem, spokenLanguageProblem(settings.speech, QStringLiteral("Local Model")));
    }

    // The Fallbacks row adds the first fallback's reason it can't stand in,
    // in the negative tone, as the mockup's "Model not downloaded" shows.
    void theFallbacksRowSaysWhyAFallbackCantStandIn()
    {
        LiveFacts facts;
        SchemaContext context = chainContext();
        context.liveFacts = [&facts] { return facts; };
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow *speech = schema.row(QStringLiteral("speechFallbacks"));
        const SettingsRow *refinement = schema.row(QStringLiteral("refinementFallbacks"));
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("local")};
        settings.speech.local.modelId = QStringLiteral("parakeet");
        settings.speech.language = QStringLiteral("en");
        QCOMPARE(speech->helpValue(settings), QStringLiteral("Local Model. No model downloaded, so it can't stand in yet."));
        QCOMPARE(speech->helpTone(settings), StatusTone::Negative);

        facts.downloadedModels = {QStringLiteral("parakeet")};
        QCOMPARE(speech->helpValue(settings), QStringLiteral("Local Model"));
        QCOMPARE(speech->helpTone(settings), StatusTone::Normal);

        settings.refinement.fallbackProviderIds = {QStringLiteral("anthropic"), QStringLiteral("local")};
        settings.refinement.localRunner.runner = QStringLiteral("ollama");
        facts.runnersChecked = true;
        QCOMPARE(refinement->helpValue(settings),
                 QStringLiteral("Anthropic, then Local Runner. Ollama isn't running, so it can't stand in right now."));
        QCOMPARE(refinement->helpTone(settings), StatusTone::Negative);
    }

    // "Use this model" makes Local Model the primary the way the Service row
    // does, and the list row reads the fallbacks as they will be saved.
    void useThisModelLeavesTheFallbacks()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        const SettingsRow &browser =
            rowById(schema.page(QStringLiteral("localModels")), QStringLiteral("localModelBrowser"));
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("local"), QStringLiteral("endpoint")};
        browser.apply(settings, QStringLiteral("parakeet"));
        QCOMPARE(settings.speech.providerId, QStringLiteral("local"));
        QCOMPARE(settings.speech.fallbackProviderIds, QStringList{QStringLiteral("endpoint")});

        settings.speech.fallbackProviderIds = {QStringLiteral("local"), QStringLiteral("endpoint")};
        QCOMPARE(schema.row(QStringLiteral("speechFallbackList"))->value(settings).toStringList(),
                 QStringList{QStringLiteral("endpoint")});
    }

    // A build without local speech keeps a saved Local Model fallback, says
    // it can't run it so it can be removed, never offers it, and has no
    // Local Model card pointing at a page it lacks.
    void aFallbackThisBuildCantRunCanOnlyBeRemoved()
    {
        SchemaContext context = chainContext();
        context.speechProviders.removeIf([](const RowOption &provider) { return provider.id == QStringLiteral("local"); });
        LiveFacts facts;
        facts.downloadedModels = {QStringLiteral("parakeet")};
        context.liveFacts = [&facts] { return facts; };
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        settings.speech.fallbackProviderIds = {QStringLiteral("local")};
        settings.speech.local.modelId = QStringLiteral("parakeet");
        settings.speech.language = QStringLiteral("en");

        const FallbackListPresentation list = fallbackListPresentation(
            ProviderRole::Speech, settings, facts, context.speechProviders, FallbackSurface::Settings);
        QCOMPARE(list.items.size(), 1);
        QCOMPARE(list.items.first().label, QStringLiteral("Provider not in this build"));
        QCOMPARE(list.items.first().status, QStringLiteral("This build of Speecher can't run it, so it can't stand in."));
        QCOMPARE(list.items.first().tone, StatusTone::Negative);
        QVERIFY(!ids(list.addChoices).contains(QStringLiteral("local")));

        const SettingsSchema schema = buildSettingsSchema(context);
        QCOMPARE(schema.row(QStringLiteral("speechFallbacks"))->helpValue(settings),
                 QStringLiteral("Provider not in this build. This build of Speecher can't run it, so it can't stand in."));
        // It never takes over from the primary either.
        facts.reachability = Reachability::Offline;
        QCOMPARE(schema.row(QStringLiteral("speechProvider"))->helpValue(settings),
                 QStringLiteral("Can't reach ChatGPT right now."));
        QVERIFY(!schema.row(QStringLiteral("speechLocalModel"))->visible(settings, Capabilities{}));
        QVERIFY(!schema.row(QStringLiteral("speechLocalModelDownload"))->visible(settings, Capabilities{}));
    }

    void aMixedChainSaysWhichProvidersReadTheScreenshot()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        const SettingsRow *screenshots = schema.row(QStringLiteral("includeScreenshotContext"));
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("local");
        QVERIFY(!screenshots->enabled(settings, Capabilities{}));
        const QString plain = screenshots->helpValue(settings);
        settings.refinement.fallbackProviderIds = {QStringLiteral("anthropic")};
        QVERIFY(screenshots->enabled(settings, Capabilities{}));
        QCOMPARE(screenshots->helpValue(settings),
                 plain + QStringLiteral(" Only OpenAI and Anthropic can read it; the others clean up without it."));
        settings.refinement.providerId = QStringLiteral("openai");
        QCOMPARE(screenshots->helpValue(settings), plain);
    }

    void theCliProxyServerShowsForAnEndpointFallbackThroughIt()
    {
        const SettingsSchema schema = buildSettingsSchema(chainContext());
        const SettingsRow *server = schema.row(QStringLiteral("cliproxyBaseUrl"));
        AppSettings settings;
        settings.refinement.endpoint.preset = QStringLiteral("cliproxy");
        QVERIFY(!server->visible(settings, Capabilities{}));
        settings.refinement.fallbackProviderIds = {QStringLiteral("endpoint")};
        QVERIFY(server->visible(settings, Capabilities{}));
    }

    void onlyADisconnectedSystemCountsAsOffline()
    {
        QCOMPARE(NetworkReachability::fromSystem(QNetworkInformation::Reachability::Online), Reachability::Online);
        QCOMPARE(NetworkReachability::fromSystem(QNetworkInformation::Reachability::Disconnected),
                 Reachability::Offline);
        // A blocked connectivity probe reports Local or Site on working networks.
        for (const auto unknown : {QNetworkInformation::Reachability::Unknown, QNetworkInformation::Reachability::Local,
                                   QNetworkInformation::Reachability::Site}) {
            QCOMPARE(NetworkReachability::fromSystem(unknown), Reachability::Unknown);
        }
    }

    // Reachability and the sign-ins seen reach the rows through LocalSetup's
    // facts, and their changes through its changed().
    void availabilityReachesTheLiveFacts()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup setup(settings, providers, models);
        NetworkReachability reachability;
        ProviderAvailability availability(reachability);
        setup.setProviderAvailability(availability);
        QSignalSpy changed(&setup, &LocalSetup::changed);

        QCOMPARE(setup.liveFacts().reachability, Reachability::Unknown);
        reachability.setReachability(Reachability::Offline);
        availability.noteSignIn(QStringLiteral("codex"), false);
        availability.noteSignIn(QStringLiteral("codex"), false);
        QCOMPARE(changed.count(), 2);
        const LiveFacts facts = setup.liveFacts();
        QCOMPARE(facts.reachability, Reachability::Offline);
        QCOMPARE(facts.signedIn.value(QStringLiteral("codex"), true), false);
        QVERIFY(!facts.signedIn.contains(QStringLiteral("claude")));
        QVERIFY(!facts.runnersChecked);
    }

    void setupSuggestsALocalFallbackOnlyWhileItCanBeAdded()
    {
        AppSettings settings;
        settings.speech.providerId = QStringLiteral("codex");
        const SetupFallbackOffer model{QStringLiteral("local"), QStringLiteral("parakeet"),
                                       QStringLiteral("Parakeet 0.6B")};
        SetupFallbackPresentation section =
            setupFallbackPresentation(ProviderRole::Speech, settings, LiveFacts{}, speechChoices(), model);
        QVERIFY(section.visible);
        QCOMPARE(section.list.heading, QStringLiteral("If ChatGPT Codex is unavailable"));
        QCOMPARE(section.suggestion, QStringLiteral("This computer can run Parakeet 0.6B, which keeps dictation "
                                                    "working without internet."));
        QCOMPARE(section.offer->modelId, QStringLiteral("parakeet"));
        // Not once it is in the chain, nor while it is the primary, whose
        // hint keeps dictation on this computer.
        settings.speech.fallbackProviderIds = {QStringLiteral("local")};
        section = setupFallbackPresentation(ProviderRole::Speech, settings, LiveFacts{}, speechChoices(), model);
        QVERIFY(section.suggestion.isEmpty() && !section.offer);
        const QString cloudHint = section.hint;
        settings.speech.providerId = QStringLiteral("local");
        section = setupFallbackPresentation(ProviderRole::Speech, settings, LiveFacts{}, speechChoices(), model);
        QVERIFY(!section.offer);
        QVERIFY(section.hint != cloudHint);

        settings.refinement.providerId = QStringLiteral("none");
        QVERIFY(!setupFallbackPresentation(ProviderRole::Refinement, settings, LiveFacts{}, refinementChoices(), {})
                     .visible);
    }

    // The approved mockup's hint, which says the raw transcript is pasted, so
    // the list's footer doesn't say it again.
    void setupRefinementHintSaysTheRawTranscriptIsPasted()
    {
        AppSettings settings;
        settings.refinement.providerId = QStringLiteral("openai");
        const SetupFallbackPresentation section =
            setupFallbackPresentation(ProviderRole::Refinement, settings, LiveFacts{}, refinementChoices(), {});
        QCOMPARE(section.hint, QStringLiteral("Speecher tries these top to bottom when it's offline, signed out or "
                                              "the provider stops answering. If none of them answers, it pastes the "
                                              "raw transcript, as it does today. Optional."));
        QVERIFY(section.list.footer.isEmpty());
    }

    void acceptingASetupOfferAddsAFallbackAndKeepsThePrimary()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        settings.setRefinementProvider(QStringLiteral("anthropic"));
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup setup(settings, providers, models);

        // A runner offers itself only with a cleanup model it serves.
        QVERIFY(!setup.setupFallbackOffer(ProviderRole::Refinement));
        DetectedRunner ollama{QStringLiteral("ollama"), QStringLiteral("Ollama"), QStringLiteral("0.34.4"),
                              QStringLiteral("http://127.0.0.1:11434/v1"), {}};
        LocalSetupTestAccess::setRunners(setup, {ollama});
        QVERIFY(!setup.setupFallbackOffer(ProviderRole::Refinement));
        ollama.models = {QStringLiteral("gemma4:e4b")};
        LocalSetupTestAccess::setRunners(setup, {ollama});
        QCOMPARE(setup.setupFallbackOffer(ProviderRole::Refinement)->name, QStringLiteral("Ollama"));
        setup.acceptSetupFallbackOffer(ProviderRole::Refinement);
        QCOMPARE(settings.refinementProvider(), QStringLiteral("anthropic"));
        QCOMPARE(settings.refinementFallbackProviders(), QStringList{QStringLiteral("local")});
        QCOMPARE(settings.localRunnerSettings().model, QStringLiteral("gemma4:e4b"));

        // A Local Model only once the hardware is known to fit it.
        settings.setSpeechProvider(QStringLiteral("codex"));
        QVERIFY(!setup.setupFallbackOffer(ProviderRole::Speech));
        HardwareProfile workstation;
        workstation.systemRamBytes = 64'000'000'000;
        workstation.availableRamBytes = 48'000'000'000;
        LocalSetupTestAccess::setHardware(setup, workstation);
        const std::optional<SetupFallbackOffer> offer = setup.setupFallbackOffer(ProviderRole::Speech);
        QVERIFY(offer);
        setup.acceptSetupFallbackOffer(ProviderRole::Speech);
        QCOMPARE(settings.speechProvider(), QStringLiteral("codex"));
        QCOMPARE(settings.speechFallbackProviders(), QStringList{QStringLiteral("local")});
        QCOMPARE(settings.localSpeechSettings().modelId, offer->modelId);
        QVERIFY(setup.downloadProgress(offer->modelId) || !setup.downloadError(offer->modelId).isEmpty());
    }

    // An offer the chain would drop changes nothing: not the chain, the
    // chosen model, a download or the runner selection.
    void aSetupOfferThatWouldNotBeAddedChangesNothing()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl(QStringLiteral("http://127.0.0.1:1")));
        LocalSetup setup(settings, providers, models);
        HardwareProfile workstation;
        workstation.systemRamBytes = 64'000'000'000;
        workstation.availableRamBytes = 48'000'000'000;
        LocalSetupTestAccess::setHardware(setup, workstation);
        LocalSetupTestAccess::setRunners(setup, {{QStringLiteral("ollama"), QStringLiteral("Ollama"),
                                                  QStringLiteral("0.34.4"), QStringLiteral("http://127.0.0.1:11434/v1"),
                                                  {QStringLiteral("gemma4:e4b")}}});

        // Local is already the primary.
        settings.setSpeechProvider(QStringLiteral("local"));
        const std::optional<SetupFallbackOffer> offer = setup.setupFallbackOffer(ProviderRole::Speech);
        QVERIFY(offer);
        LocalSpeechSettings chosen = settings.localSpeechSettings();
        chosen.modelId = std::find_if(localModelCatalog().cbegin(), localModelCatalog().cend(),
                                      [&offer](const LocalModel &model) { return model.id != offer->modelId; })
                             ->id;
        settings.setLocalSpeechSettings(chosen);
        setup.acceptSetupFallbackOffer(ProviderRole::Speech);
        QVERIFY(settings.speechFallbackProviders().isEmpty());
        QCOMPARE(settings.localSpeechSettings().modelId, chosen.modelId);
        QVERIFY(!setup.downloadProgress(offer->modelId) && setup.downloadError(offer->modelId).isEmpty());

        // The chain is full.
        settings.setRefinementProvider(QStringLiteral("anthropic"));
        const QStringList full = {QStringLiteral("openai"), QStringLiteral("endpoint")};
        settings.setRefinementFallbackProviders(full);
        QCOMPARE(settings.refinementFallbackProviders().size(), kMaxFallbackProviders);
        QVERIFY(setup.setupFallbackOffer(ProviderRole::Refinement));
        setup.acceptSetupFallbackOffer(ProviderRole::Refinement);
        QCOMPARE(settings.refinementFallbackProviders(), full);
        QCOMPARE(settings.localRunnerSettings(), LocalRunnerSettings{});
    }

    void endpointAndRunnerRowsReportWhatTheAppLayerLearned()
    {
        SchemaContext context = fakeContext();
        LiveFacts facts;
        facts.speechEndpointStatus = QStringLiteral("Connected. Models available: 2.");
        facts.speechEndpointModels = {QStringLiteral("whisper-1"), QStringLiteral("parakeet")};
        facts.runners = {{QStringLiteral("ollama"), QStringLiteral("Ollama 0.34.4")}};
        facts.runnerModels.insert(QStringLiteral("ollama"), {QStringLiteral("gemma4:e4b")});
        context.liveFacts = [&facts] { return facts; };
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsPage &audio = schema.page(QStringLiteral("audio"));
        const SettingsPage &refinement = schema.page(QStringLiteral("refinement"));
        AppSettings settings;
        settings.refinement.localRunner.runner = QStringLiteral("ollama");

        QCOMPARE(rowById(audio, QStringLiteral("speechEndpointTest")).helpValue(settings), facts.speechEndpointStatus);
        QCOMPARE(rowById(audio, QStringLiteral("speechEndpointModel")).suggestions(settings).size(), 2);
        QCOMPARE(rowById(refinement, QStringLiteral("refinementEndpointTest")).helpValue(settings),
                 QStringLiteral("Not tested yet."));
        QCOMPARE(rowById(refinement, QStringLiteral("localRunnerModel")).suggestions(settings).first().id,
                 QStringLiteral("gemma4:e4b"));
        QCOMPARE(rowById(refinement, QStringLiteral("localRunner")).options(settings).size(), 1);

        // A saved runner that is not answering stays selectable and says so.
        facts.runners.clear();
        const QList<RowOption> runners = rowById(refinement, QStringLiteral("localRunner")).options(settings);
        QCOMPARE(runners.size(), 1);
        QCOMPARE(runners.first().label, QStringLiteral("Ollama (not running)"));
    }

    void localAccelerationAndGraphicsCardAreSeparateChoices()
    {
        const QString bus = QStringLiteral("0000:01:00.0");
        const QString igpu = QStringLiteral("0000:05:00.0");
        const QList<LocalGpu> gpus{
            {QStringLiteral("cuda"), bus, QStringLiteral("NVIDIA GeForce RTX 3060")},
            {QStringLiteral("vulkan"), bus, QStringLiteral("NVIDIA GeForce RTX 3060")},
            {QStringLiteral("vulkan"), igpu, QStringLiteral("AMD Radeon Graphics")},
        };
        const auto labels = [](const QList<RowOption> &options) {
            QStringList result;
            for (const RowOption &option : options) {
                result.append(option.label);
            }
            return result;
        };

        // Only backends that reach a card here, in localBackends order.
        QCOMPARE(labels(localAccelerationOptions(gpus, {})),
                 (QStringList{QStringLiteral("Automatic"), QStringLiteral("CPU"), QStringLiteral("Vulkan"),
                              QStringLiteral("CUDA")}));
        QCOMPARE(labels(localAccelerationOptions({}, {})),
                 (QStringList{QStringLiteral("Automatic"), QStringLiteral("CPU")}));

        // Cards follow the backend; the CPU and Automatic pick none.
        QCOMPARE(labels(localGraphicsCardOptions(gpus, {QStringLiteral("vulkan"), bus})),
                 (QStringList{QStringLiteral("NVIDIA GeForce RTX 3060"), QStringLiteral("AMD Radeon Graphics")}));
        QCOMPARE(localGraphicsCardOptions(gpus, {QStringLiteral("cuda"), bus}).size(), 1);
        QVERIFY(localGraphicsCardOptions(gpus, {QStringLiteral("cpu"), QString()}).isEmpty());
        QVERIFY(localGraphicsCardOptions(gpus, {}).isEmpty());

        // Saved choices this computer lacks stay selected, disabled.
        const QList<RowOption> backends = localAccelerationOptions({}, {QStringLiteral("cuda"), bus});
        QCOMPARE(backends.last().label, QStringLiteral("CUDA (not available)"));
        QVERIFY(!backends.last().enabled);
        const QList<RowOption> cards = localGraphicsCardOptions({}, {QStringLiteral("cuda"), bus});
        QCOMPARE(cards.size(), 1);
        QCOMPARE(cards.first().label, QStringLiteral("Missing graphics card"));
        QVERIFY(!cards.first().enabled);

        // A card saved before acceleration could be chosen is kept, as that card.
        const QList<RowOption> legacy = localGraphicsCardOptions(gpus, {QStringLiteral("auto"), bus});
        QCOMPARE(labels(legacy), QStringList{QStringLiteral("NVIDIA GeForce RTX 3060")});
    }

    void localModelsPageFollowsRefinement()
    {
        SchemaContext context = fakeContext();
        context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
        const SettingsSchema schema = buildSettingsSchema(context);
        QStringList pageIds;
        for (const SettingsPage &page : schema.pages) {
            pageIds.append(page.id);
        }
        QCOMPARE(pageIds.indexOf(QStringLiteral("localModels")),
                 pageIds.indexOf(QStringLiteral("refinement")) + 1);
        const SettingsPage &page = schema.page(QStringLiteral("localModels"));
        const SettingsRow &browser = rowById(page, QStringLiteral("localModelBrowser"));
        QCOMPARE(browser.kind, RowKind::Custom);

        // Choosing a model is choosing to dictate with it.
        AppSettings settings;
        QCOMPARE(browser.value(settings).toString(), QString());
        browser.apply(settings, QStringLiteral("moonshine-small"));
        QCOMPARE(settings.speech.providerId, QStringLiteral("local"));
        QCOMPARE(settings.speech.local.modelId, QStringLiteral("moonshine-small"));
        QCOMPARE(browser.value(settings).toString(), QStringLiteral("moonshine-small"));

        // Acceleration is always a choice, even with no graphics card; the
        // card row only shows when there is a card to choose between.
        const SettingsRow &acceleration = rowById(page, QStringLiteral("localAcceleration"));
        const SettingsRow &card = rowById(page, QStringLiteral("localGraphicsCard"));
        QVERIFY(!acceleration.visible || acceleration.visible(settings, Capabilities{}));
        QCOMPARE(acceleration.value(settings).toString(), QStringLiteral("auto"));
        QVERIFY(!card.visible(settings, Capabilities{}));
        acceleration.apply(settings, QStringLiteral("cpu"));
        QCOMPARE(settings.speech.local.runsOn, (LocalRunsOn{QStringLiteral("cpu"), QString()}));
        card.apply(settings, QStringLiteral("0000:c1:00.0"));
        acceleration.apply(settings, QStringLiteral("auto"));
        QCOMPARE(settings.speech.local.runsOn, LocalRunsOn{});

        // It is its own pane in the Speech group, after Dictation.
        const QStringList &speech = schema.sidebarGroups.at(1).panes;
        QCOMPARE(speech.indexOf(QStringLiteral("localModels")), speech.indexOf(QStringLiteral("dictation")) + 1);

        // A build that cannot run speech models has no page or pane for them.
        const SettingsSchema without = buildSettingsSchema(fakeContext());
        QVERIFY(!without.hasPage(QStringLiteral("localModels")));
        QVERIFY(std::none_of(without.panes.cbegin(), without.panes.cend(), [](const SettingsPane &pane) {
            return pane.id == QStringLiteral("localModels");
        }));
        for (const SidebarGroup &group : without.sidebarGroups) {
            QVERIFY(!group.panes.contains(QStringLiteral("localModels")));
        }
    }

    void audioPageChoosesTheLocalModel()
    {
        SchemaContext context = fakeContext();
        context.speechProviders.append({QStringLiteral("local"), QStringLiteral("Local model")});
        LiveFacts facts;
        context.liveFacts = [&facts] { return facts; };
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsPage &audio = schema.page(QStringLiteral("audio"));
        const SettingsRow &model = rowById(audio, QStringLiteral("speechLocalModel"));
        const SettingsRow &download = rowById(audio, QStringLiteral("speechLocalModelDownload"));
        AppSettings settings;

        // Only while Local model is the speech provider.
        settings.speech.providerId = QStringLiteral("claude");
        facts.downloadedModels = {QStringLiteral("moonshine-small")};
        QVERIFY(!model.visible(settings, Capabilities{}));
        QVERIFY(!download.visible(settings, Capabilities{}));

        // Nothing downloaded: the row points at the Local models page instead.
        settings.speech.providerId = QStringLiteral("local");
        facts.downloadedModels.clear();
        QVERIFY(!model.visible(settings, Capabilities{}));
        QVERIFY(download.visible(settings, Capabilities{}));
        QCOMPARE(download.kind, RowKind::Action);

        // Downloaded models are the choices, and a chosen model that is not
        // downloaded stays shown but cannot be picked again.
        facts.downloadedModels = {QStringLiteral("moonshine-small"), QStringLiteral("parakeet")};
        settings.speech.local.modelId = QStringLiteral("cohere");
        QVERIFY(model.visible(settings, Capabilities{}));
        QVERIFY(!download.visible(settings, Capabilities{}));
        const QList<RowOption> options = model.options(settings);
        QCOMPARE(options.size(), 3);
        QCOMPARE(options.at(0).id, QStringLiteral("moonshine-small"));
        QCOMPARE(options.at(0).label, QStringLiteral("Moonshine Small"));
        QCOMPARE(options.at(1).id, QStringLiteral("parakeet"));
        QCOMPARE(options.at(2).label, QStringLiteral("Cohere Transcribe (not downloaded)"));
        QVERIFY(!options.at(2).enabled);

        // It writes the setting "Use this model" writes.
        model.apply(settings, QStringLiteral("parakeet"));
        QCOMPARE(settings.speech.local.modelId, QStringLiteral("parakeet"));
        QVERIFY(settings.speech.local.modelChosen);
        QCOMPARE(model.value(settings).toString(), QStringLiteral("parakeet"));
        const SettingsRow &browser =
            rowById(schema.page(QStringLiteral("localModels")), QStringLiteral("localModelBrowser"));
        QCOMPARE(browser.value(settings).toString(), QStringLiteral("parakeet"));
        browser.apply(settings, QStringLiteral("moonshine-small"));
        QCOMPARE(model.value(settings).toString(), QStringLiteral("moonshine-small"));
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

    // Under the primary picker, the chosen provider's Rating and, on
    // Dictation, an Advanced list of its models. Custom Endpoint and "none"
    // show neither.
    void ratingRowsFollowTheChosenProvider()
    {
        SchemaContext context = chainContext();
        // A Ryzen 4750U laptop's integrated graphics.
        HardwareProfile laptop;
        laptop.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
        laptop.chipName = QStringLiteral("AMD Ryzen 7 PRO 4750U with Radeon Graphics");
        laptop.systemRamBytes = quint64(16) << 30;
        laptop.availableRamBytes = quint64(12) << 30;
        context.hardware = [laptop] { return laptop; };
        const SettingsSchema schema = buildSettingsSchema(context);
        const SettingsRow &speechRating = rowById(schema.page(QStringLiteral("audio")), QStringLiteral("speechRating"));
        const SettingsRow &speechModels = rowById(schema.page(QStringLiteral("audio")), QStringLiteral("speechModels"));
        const SettingsRow &refinementRating =
            rowById(schema.page(QStringLiteral("refinement")), QStringLiteral("refinementRating"));
        QCOMPARE(speechRating.kind, RowKind::Rating);
        QCOMPARE(speechRating.label, QStringLiteral("Rating"));
        QCOMPARE(speechModels.kind, RowKind::ModelList);
        QCOMPARE(speechModels.label, QStringLiteral("Advanced"));
        QCOMPARE(refinementRating.kind, RowKind::Rating);
        const auto bars = [](const SettingsRow &row, const AppSettings &settings) {
            QStringList texts;
            for (const Rating &bar : row.ratings(settings)) {
                texts.append(ratingMeasureLabel(bar.measure) + QLatin1Char(' ') + ratingValueText(bar));
            }
            return texts;
        };
        const auto names = [](const SettingsRow &row, const AppSettings &settings) {
            QStringList models;
            for (const RatedModel &model : row.ratedModels(settings)) {
                models.append(model.name);
            }
            return models;
        };
        const Capabilities capabilities;
        AppSettings settings;

        settings.speech.providerId = QStringLiteral("codex");
        QVERIFY(speechRating.visible(settings, capabilities));
        QCOMPARE(speechRating.helpValue(settings), QStringLiteral("Out of 10."));
        QCOMPARE(bars(speechRating, settings),
                 QStringList({QStringLiteral("Accuracy 8.5/10"), QStringLiteral("Speed 7/10")}));
        QVERIFY(speechModels.visible(settings, capabilities));
        QCOMPARE(speechModels.helpValue(settings), QStringLiteral("The models ChatGPT Codex uses"));
        QCOMPARE(names(speechModels, settings),
                 QStringList({QStringLiteral("GPT Live Transcribe"), QStringLiteral("GPT Transcribe")}));

        settings.speech.providerId = QStringLiteral("local");
        QCOMPARE(speechRating.helpValue(settings), QStringLiteral("Parakeet 0.6B on this computer"));
        QCOMPARE(speechModels.helpValue(settings), QStringLiteral("The models you can run on this computer"));
        QCOMPARE(names(speechModels, settings).size(), localModelCatalog().size());

        settings.speech.providerId = QStringLiteral("endpoint");
        QVERIFY(!speechRating.visible(settings, capabilities));
        QVERIFY(!speechModels.visible(settings, capabilities));

        settings.refinement.providerId = QStringLiteral("openai");
        QVERIFY(refinementRating.visible(settings, capabilities));
        QCOMPARE(refinementRating.helpValue(settings), QStringLiteral("Out of 10."));
        QCOMPARE(bars(refinementRating, settings),
                 QStringList({QStringLiteral("Quality 10/10"), QStringLiteral("Speed 7/10")}));
        settings.refinement.providerId = QStringLiteral("local");
        QCOMPARE(refinementRating.helpValue(settings), QStringLiteral("LFM2.5 1.2B, suggested for this computer"));
        QCOMPARE(bars(refinementRating, settings),
                 QStringList({QStringLiteral("Quality 5.5/10"), QStringLiteral("Speed 7/10")}));
        for (const QString &id : {QStringLiteral("endpoint"), QStringLiteral("none")}) {
            settings.refinement.providerId = id;
            QVERIFY2(!refinementRating.visible(settings, capabilities), qPrintable(id));
        }
    }
};

int runSettingsSchemaTests(int argc, char **argv)
{
    SettingsSchemaTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_settings_schema.moc"
