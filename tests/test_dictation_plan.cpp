// planDictation, the one resolver for a run: one row per cell of the layers
// table (Defaults, App Type, App, Writing Profile, this run), each with the
// value the run gets and where the plan says it came from.
#include "common/test_suites.h"
#include "common/apps_fixtures.h"
#include "common/vocabulary_entries.h"

#include "core/OutputFormat.h"
#include "core/ProviderChain.h"
#include "core/ProviderTable.h"
#include "dictation/DictationPlan.h"
#include "dictation/TranscriptPipeline.h"

#include <QTest>

#include <functional>

using namespace speecher;
using namespace speecher::test;

namespace {

const QString kThunderbirdId = QStringLiteral("org.mozilla.Thunderbird");

Target thunderbird()
{
    Target target;
    target.applicationId = kThunderbirdId;
    target.applicationName = QStringLiteral("Thunderbird");
    target.processName = QStringLiteral("thunderbird");
    return target;
}

Target slack()
{
    Target target;
    target.applicationId = QStringLiteral("slack");
    target.applicationName = QStringLiteral("Slack");
    target.processName = QStringLiteral("slack");
    return target;
}

Target konsole()
{
    Target target;
    target.applicationId = QStringLiteral("org.kde.konsole");
    target.applicationName = QStringLiteral("Konsole");
    target.processName = QStringLiteral("konsole");
    target.role = QStringLiteral("terminal");
    return target;
}

WritingProfileSettings &profile(AppSettings &settings, const QString &id)
{
    for (WritingProfileSettings &each : settings.refinement.writingProfiles) {
        if (each.profile == id) {
            return each;
        }
    }
    settings.refinement.writingProfiles.append(writingProfileSettingsFor({}, id));
    return settings.refinement.writingProfiles.last();
}

const DeliverySteps kKeepOnly{.insert = false, .press = false, .keep = true};
const DeliverySteps kInsertAndKeep{.insert = true, .press = false, .keep = true};

void setTypePasting(AppSettings &settings, AppCategory type, const PastingChoice &pasting)
{
    for (AppTypeSettings &each : settings.apps.types) {
        if (each.type == type) {
            each.pasting = pasting;
        }
    }
}

// Speech and refinement in a known order, so a row that changes nothing
// reads the page's first service.
AppSettings savedSettings()
{
    AppSettings settings;
    settings.speech.language = QStringLiteral("en");
    settings.speech.services.order = {QStringLiteral("claude"), QStringLiteral("codex")};
    settings.refinement.enabled = true;
    settings.refinement.services.order = {QStringLiteral("openai"), QStringLiteral("anthropic")};
    settings.vocabulary = {VocabularyEntry{QStringLiteral("Kubernetes")}};
    return settings;
}

using SetUp = std::function<void(AppSettings &, Target &, RunOverrides &)>;
using Read = std::function<QString(const DictationPlan &, const Target &)>;

const Read writingProfile = [](const DictationPlan &plan, const Target &) { return plan.profile.profile; };
const Read pasting = [](const DictationPlan &plan, const Target &) {
    return plan.app.pasting.steps ? DeliveryPlan{*plan.app.pasting.steps}.route().join(u'+') : QStringLiteral("steps");
};
const Read outputFormat = [](const DictationPlan &plan, const Target &) {
    return outputFormatName(plan.settings.output.format);
};
const Read speech = [](const DictationPlan &plan, const Target &) {
    const QString service = plan.settings.speech.services.main();
    const QString model = plan.settings.speech.services.choice(service).model;
    return model.isEmpty() ? service : service + QLatin1Char('/') + model;
};
const Read refinement = [](const DictationPlan &plan, const Target &) {
    return serviceChain(plan.settings.refinement).value(0, kNoRefinementProvider);
};
const Read spokenLanguage = [](const DictationPlan &plan, const Target &) { return plan.settings.speech.language; };
const Read cleanup = [](const DictationPlan &plan, const Target &) { return plan.settings.refinement.style; };
const Read tone = [](const DictationPlan &plan, const Target &) { return plan.profile.tone; };
const Read outputLanguage = [](const DictationPlan &plan, const Target &) { return plan.profile.outputLanguage; };
const Read instructions = [](const DictationPlan &plan, const Target &) { return plan.profile.instructions; };
// The terms the speech request carries.
const Read speechTerms = [](const DictationPlan &plan, const Target &) {
    return plan.settings.speech.vocabulary.join(QLatin1Char(','));
};
// What a Learned Correction for "tee three" makes of it in this app.
const Read corrected = [](const DictationPlan &plan, const Target &target) {
    return TranscriptPipeline::prepare(QStringLiteral("tee three"), plan, target, QStringLiteral("en"))
        .bindingResult.boundText;
};

struct Cell {
    // "<setting>: <layer>", as the table names them.
    const char *name;
    SetUp setUp;
    // The plan's source key.
    QString field;
    Read read;
    QString value;
    QString source;
};

QList<Cell> layerTable()
{
    const SetUp nothing = [](AppSettings &, Target &, RunOverrides &) {};
    const SetUp workRun = [](AppSettings &, Target &, RunOverrides &run) { run.writingProfile = WritingProfile::Work; };
    const QString work = QStringLiteral("profile:") + WritingProfile::Work;
    const QString run = QStringLiteral("run");
    const QString byDefault = QStringLiteral("default");
    return {
        // Writing profile: Other apps, App Type, App, this run.
        {"writing profile: defaults (Other apps)",
         [](AppSettings &settings, Target &, RunOverrides &) { setOtherAppsProfile(settings.apps, WritingProfile::Personal); },
         QStringLiteral("writingProfile"), writingProfile, WritingProfile::Personal, byDefault},
        {"writing profile: app type",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             AppEntry mailer = idApp(QStringLiteral("org.example.Mailer"));
             mailer.type = AppCategory::Email;
             settings.apps.apps = {mailer};
             target.applicationId = QStringLiteral("org.example.Mailer");
         },
         QStringLiteral("writingProfile"), writingProfile, WritingProfile::Email, QStringLiteral("type:email")},
        // The catalog types and names apps nobody set: its profile, or its
        // type's, comes from it.
        {"writing profile: the catalog's for a known app",
         [](AppSettings &, Target &target, RunOverrides &) { target = slack(); },
         QStringLiteral("writingProfile"), writingProfile, WritingProfile::Work, QStringLiteral("builtin")},
        {"writing profile: app",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             settings.apps.apps = {idApp(kThunderbirdId, WritingProfile::Personal)};
             target = thunderbird();
         },
         QStringLiteral("writingProfile"), writingProfile, WritingProfile::Personal, QStringLiteral("app:") + kThunderbirdId},
        {"writing profile: run",
         [](AppSettings &settings, Target &target, RunOverrides &run) {
             settings.apps.apps = {idApp(kThunderbirdId, WritingProfile::Personal)};
             target = thunderbird();
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("writingProfile"), writingProfile, WritingProfile::Work, run},

        // Pasting: the steps, App Type (Terminals' for a terminal), App;
        // a run changes only the output format.
        {"pasting: defaults (the steps)", nothing, QStringLiteral("pasting"), pasting, QStringLiteral("steps"), byDefault},
        {"pasting: app type",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             setTypePasting(settings, AppCategory::Email, {kKeepOnly, std::nullopt});
             target = thunderbird();
         },
         QStringLiteral("pasting"), pasting, QStringLiteral("keep"), QStringLiteral("type:email")},
        {"pasting: terminals' for a terminal",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             setTypePasting(settings, AppCategory::Terminal, {kInsertAndKeep, std::nullopt});
             target = konsole();
         },
         QStringLiteral("pasting"), pasting, QStringLiteral("insert+keep"), QStringLiteral("type:terminal")},
        {"pasting: app",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             setTypePasting(settings, AppCategory::Email, {kKeepOnly, std::nullopt});
             settings.apps.apps = {idApp(kThunderbirdId, std::nullopt, {kInsertAndKeep, std::nullopt})};
             target = thunderbird();
         },
         QStringLiteral("pasting"), pasting, QStringLiteral("insert+keep"), QStringLiteral("app:") + kThunderbirdId},
        {"output format: defaults", nothing, QStringLiteral("outputFormat"), outputFormat,
         outputFormatName(AppSettings().output.format), byDefault},
        {"output format: run (--format)",
         [](AppSettings &, Target &, RunOverrides &run) { run.outputFormat = OutputFormat::Html; },
         QStringLiteral("outputFormat"), outputFormat, outputFormatName(OutputFormat::Html), run},

        // Speech service: Speech to text, the profile's first, this run.
        {"speech service: defaults (Speech to text)", workRun, QStringLiteral("speech"), speech,
         QStringLiteral("claude"), byDefault},
        {"speech service: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).speechFirst = ServiceChoice{QStringLiteral("codex")};
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("speech"), speech, QStringLiteral("codex"), work},
        {"speech service: run",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).speechFirst = ServiceChoice{QStringLiteral("codex")};
             run.writingProfile = WritingProfile::Work;
             run.speech = ServiceChoice{QStringLiteral("endpoint"), QStringLiteral("whisper-1")};
         },
         QStringLiteral("speech"), speech, QStringLiteral("endpoint/whisper-1"), run},
        {"speech service: run naming the profile's keeps its model",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).speechFirst =
                 ServiceChoice{QStringLiteral("endpoint"), QStringLiteral("whisper-1")};
             run.writingProfile = WritingProfile::Work;
             run.speech = ServiceChoice{QStringLiteral("endpoint")};
         },
         QStringLiteral("speech"), speech, QStringLiteral("endpoint/whisper-1"), run},

        // Refinement service: Refinement, the profile's first, this run.
        {"refinement service: defaults (Refinement)", workRun, QStringLiteral("refinement"), refinement,
         QStringLiteral("openai"), byDefault},
        {"refinement service: defaults with Clean up my words off",
         [](AppSettings &settings, Target &, RunOverrides &) { settings.refinement.enabled = false; },
         QStringLiteral("refinement"), refinement, kNoRefinementProvider, byDefault},
        {"refinement service: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).refinementFirst = ServiceChoice{QStringLiteral("anthropic")};
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("refinement"), refinement, QStringLiteral("anthropic"), work},
        {"refinement service: profile, with Clean up my words off",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             settings.refinement.enabled = false;
             profile(settings, WritingProfile::Work).refinementFirst = ServiceChoice{QStringLiteral("anthropic")};
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("refinement"), refinement, kNoRefinementProvider, byDefault},
        {"refinement service: run, with Clean up my words off",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             settings.refinement.enabled = false;
             run.refinement = ServiceChoice{QStringLiteral("anthropic")};
         },
         QStringLiteral("refinement"), refinement, QStringLiteral("anthropic"), run},
        {"refinement service: run refining nothing (--refine none)",
         [](AppSettings &, Target &, RunOverrides &run) { run.refinement = ServiceChoice{kNoRefinementProvider}; },
         QStringLiteral("refinement"), refinement, kNoRefinementProvider, run},

        // Spoken language: Speech to text, this run.
        {"spoken language: defaults (Speech to text)", nothing, QStringLiteral("spokenLanguage"), spokenLanguage,
         QStringLiteral("en"), byDefault},
        {"spoken language: run (--language)",
         [](AppSettings &, Target &, RunOverrides &run) { run.spokenLanguage = QStringLiteral("de"); },
         QStringLiteral("spokenLanguage"), spokenLanguage, QStringLiteral("de"), run},

        // Cleanup and tone: the profile, this run.
        {"cleanup: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).cleanupStrength = QStringLiteral("strong_polish");
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("cleanup"), cleanup, QStringLiteral("strong_polish"), work},
        {"cleanup: run (--cleanup)",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).cleanupStrength = QStringLiteral("strong_polish");
             run.writingProfile = WritingProfile::Work;
             run.cleanupLevel = QStringLiteral("light_cleanup");
         },
         QStringLiteral("cleanup"), cleanup, QStringLiteral("light_cleanup"), run},
        {"tone: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).tone = QStringLiteral("formal");
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("tone"), tone, QStringLiteral("formal"), work},
        {"tone: run (--tone)",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).tone = QStringLiteral("formal");
             run.writingProfile = WritingProfile::Work;
             run.tone = QStringLiteral("casual");
         },
         QStringLiteral("tone"), tone, QStringLiteral("casual"), run},

        // Output language and instructions: the profile only.
        {"output language: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).outputLanguage = QStringLiteral("Spanish");
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("outputLanguage"), outputLanguage, QStringLiteral("Spanish"), work},
        {"instructions: profile",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             profile(settings, WritingProfile::Work).instructions = QStringLiteral("Use bullet points.");
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("instructions"), instructions, QStringLiteral("Use bullet points."), work},

        // Words: the saved list, limited to an App, limited to a profile,
        // and this run's (--vocab-file, --no-vocabulary).
        {"words: defaults (the saved list)", nothing, QStringLiteral("vocabulary"), speechTerms,
         QStringLiteral("Kubernetes"), byDefault},
        {"words: limited to an app, in it",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             settings.vocabulary.append(
                 test::learnedEntry(QStringLiteral("tee three"), QStringLiteral("T3"), kThunderbirdId));
             target = thunderbird();
         },
         QStringLiteral("vocabulary"), corrected, QStringLiteral("T3"), byDefault},
        {"words: limited to an app, elsewhere",
         [](AppSettings &settings, Target &target, RunOverrides &) {
             settings.vocabulary.append(
                 test::learnedEntry(QStringLiteral("tee three"), QStringLiteral("T3"), kThunderbirdId));
             target = slack();
         },
         QStringLiteral("vocabulary"), corrected, QStringLiteral("tee three"), byDefault},
        {"words: limited to a profile, under it",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             settings.vocabulary.first().profiles = {WritingProfile::Email};
             run.writingProfile = WritingProfile::Email;
         },
         QStringLiteral("vocabulary"), speechTerms, QStringLiteral("Kubernetes"), byDefault},
        {"words: limited to a profile, under another",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             settings.vocabulary.first().profiles = {WritingProfile::Email};
             run.writingProfile = WritingProfile::Work;
         },
         QStringLiteral("vocabulary"), speechTerms, QString(), byDefault},
        {"words: run's own first (--vocab-file)",
         [](AppSettings &, Target &, RunOverrides &run) { run.addedVocabulary = {QStringLiteral("Speecher")}; },
         QStringLiteral("vocabulary"), speechTerms, QStringLiteral("Speecher,Kubernetes"), run},
        {"words: run without the saved list (--no-vocabulary)",
         [](AppSettings &, Target &, RunOverrides &run) { run.applyVocabulary = false; },
         QStringLiteral("vocabulary"), speechTerms, QString(), run},
        {"words: run without the saved list replaces nothing",
         [](AppSettings &settings, Target &, RunOverrides &run) {
             settings.vocabulary.append(test::learnedEntry(QStringLiteral("tee three"), QStringLiteral("T3")));
             run.applyVocabulary = false;
         },
         QStringLiteral("vocabulary"), corrected, QStringLiteral("tee three"), run},
    };
}

} // namespace

class DictationPlanTests : public QObject {
    Q_OBJECT

private slots:
    void everyLayerCell()
    {
        for (const Cell &cell : layerTable()) {
            AppSettings settings = savedSettings();
            Target target;
            RunOverrides run;
            cell.setUp(settings, target, run);
            const DictationPlan plan = planDictation(settings, target, run);
            const QString value = cell.read(plan, target);
            const QString source = plan.sources.value(cell.field);
            QVERIFY2(value == cell.value,
                     qPrintable(QStringLiteral("%1: value %2, expected %3").arg(QLatin1String(cell.name), value, cell.value)));
            QVERIFY2(source == cell.source,
                     qPrintable(QStringLiteral("%1: from %2, expected %3").arg(QLatin1String(cell.name), source, cell.source)));
        }
    }

    // A profile's speech service that can't listen for the run's language,
    // or that this build lacks, is passed over for the page's.
    void aProfilesSpeechServiceThatCantRunIsPassedOver()
    {
        AppSettings settings = savedSettings();
        profile(settings, WritingProfile::Work).speechFirst =
            ServiceChoice{QStringLiteral("local"), QStringLiteral("parakeet")};
        RunOverrides run;
        run.writingProfile = WritingProfile::Work;
        run.spokenLanguage = QStringLiteral("ja");
        DictationPlan plan = planDictation(settings, Target{}, run);
        QCOMPARE(plan.settings.speech.services.main(), QStringLiteral("claude"));
        QCOMPARE(plan.sources.value(QStringLiteral("speech")), QStringLiteral("default"));

        run.spokenLanguage.reset();
        plan = planDictation(settings, Target{}, run, [](const QString &service) { return service != QStringLiteral("local"); });
        QCOMPARE(plan.settings.speech.services.main(), QStringLiteral("claude"));
        plan = planDictation(settings, Target{}, run);
        QCOMPARE(plan.settings.speech.services.main(), QStringLiteral("local"));
    }

    // A Recording walks the speech Service Order's services that stream final
    // text, Local Models passed over, led by its own first service where that
    // streams; each keeps its model and language. Nothing that streams is no
    // order at all.
    void planRecordingWalksWhatStreams_data()
    {
        QTest::addColumn<QStringList>("order");
        QTest::addColumn<QString>("first");
        QTest::addColumn<QStringList>("walked");
        const QString claude = QStringLiteral("claude");
        const QString codex = QStringLiteral("codex");
        const QString local = QStringLiteral("local");
        const QString endpoint = QStringLiteral("endpoint");
        QTest::newRow("main streams") << QStringList{claude, local, codex} << QString()
                                      << QStringList{claude, local, codex};
        QTest::newRow("main is local") << QStringList{local, codex} << QString() << QStringList{local, codex};
        QTest::newRow("none can") << QStringList{endpoint} << QString() << QStringList{};
        QTest::newRow("passes over what can't") << QStringList{endpoint, local} << QString() << QStringList{local};
        QTest::newRow("own first") << QStringList{claude, codex} << codex << QStringList{codex, claude};
        QTest::newRow("own first outside the order") << QStringList{claude} << codex << QStringList{codex, claude};
        QTest::newRow("own first can't stream") << QStringList{claude, codex} << endpoint << QStringList{claude, codex};
        QTest::newRow("own first is local") << QStringList{claude, local} << local << QStringList{local, claude};
    }

    void planRecordingWalksWhatStreams()
    {
        QFETCH(QStringList, order);
        QFETCH(QString, first);
        QFETCH(QStringList, walked);
        AppSettings settings = savedSettings();
        settings.speech.services.order = order;
        settings.speech.services.choice(QStringLiteral("codex")).language = QStringLiteral("de");
        if (!first.isEmpty()) {
            settings.recording.first = ServiceChoice{first};
        }
        // The speech server here uploads each utterance whole.
        const auto streams = [](const QString &service, const SpeechSettings &) {
            return service != QStringLiteral("endpoint");
        };
        const AppSettings recording = planRecording(settings, {}, streams);
        QCOMPARE(recording.speech.services.order, walked);
        QCOMPARE(recording.speech.services.choice(QStringLiteral("codex")).language, QStringLiteral("de"));
    }

    // Its own first service's model runs; the words are those that apply to
    // Other apps' profile and no app, led by the recording's own.
    void planRecordingTakesItsFirstModelAndOtherAppsWords()
    {
        AppSettings settings = savedSettings();
        settings.speech.services.order = {QStringLiteral("endpoint")};
        settings.speech.services.choice(QStringLiteral("endpoint")).model = QStringLiteral("whisper-1");
        settings.recording.first = ServiceChoice{QStringLiteral("endpoint"), QStringLiteral("gpt-realtime")};
        VocabularyEntry everywhere{QStringLiteral("Speecher")};
        VocabularyEntry inEmail{QStringLiteral("Regards")};
        inEmail.profiles = {WritingProfile::Email};
        settings.vocabulary = {everywhere, inEmail};
        const StreamsFinalText streams = [](const QString &, const SpeechSettings &) { return true; };
        const AppSettings recording = planRecording(settings, {QStringLiteral("kwin")}, streams);
        QCOMPARE(recording.speech.services.choice(QStringLiteral("endpoint")).model, QStringLiteral("gpt-realtime"));
        QCOMPARE(recording.speech.vocabulary, (QStringList{QStringLiteral("kwin"), QStringLiteral("Speecher")}));

        // Other apps' chosen profile, not the built-in Other profile; and
        // Recordings' own choice over both.
        settings.apps.types.last().writingProfile = WritingProfile::Email;
        QCOMPARE(planRecording(settings, {}, streams).speech.vocabulary,
                 (QStringList{QStringLiteral("Regards"), QStringLiteral("Speecher")}));
        settings.recording.writingProfile = WritingProfile::Work;
        QCOMPARE(planRecording(settings, {}, streams).speech.vocabulary, QStringList{QStringLiteral("Speecher")});
    }
};

int runDictationPlanTests(int argc, char **argv)
{
    DictationPlanTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_dictation_plan.moc"
