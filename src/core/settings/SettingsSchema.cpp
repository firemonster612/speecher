#include "core/settings/SettingsSchema.h"

#include "core/EndpointSettings.h"
#include "core/SecretStore.h"

#include "core/EndpointUrl.h"
#include "core/LocalModelCatalog.h"
#include "core/OutputMethod.h"
#include "core/ReleaseNotesPresentation.h"

#include "core/BindingProcessor.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

static void initializeReleaseNotesResource()
{
    Q_INIT_RESOURCE(release_notes);
    Q_INIT_RESOURCE(release_history);
}

namespace speecher {

namespace {

using Getter = std::function<QString(const AppSettings &)>;
using Setter = std::function<void(AppSettings &, const QString &)>;
using Options = std::function<QList<RowOption>(const AppSettings &)>;

const QString kMatchColumn = QStringLiteral("match");
const QString kCategoryColumn = QStringLiteral("category");
const QString kProfileColumn = QStringLiteral("profile");
const QString kSourceColumn = QStringLiteral("source");
const QString kEnabledColumn = QStringLiteral("enabled");
const QString kApplicationColumn = QStringLiteral("application");
const QString kMethodColumn = QStringLiteral("method");
const QString kStarColumn = QStringLiteral("starred");
const QString kTermColumn = QStringLiteral("term");
const QString kUsesColumn = QStringLiteral("uses");
const QString kLastUsedColumn = QStringLiteral("lastUsed");
const QString kContextColumn = QStringLiteral("context");
const QString kKeyTermColumn = QStringLiteral("keyTerm");
const QString kProfilesColumn = QStringLiteral("profiles");
const QString kHeardColumn = QStringLiteral("original");
const QString kCorrectedColumn = QStringLiteral("corrected");
const QString kCorrectedAppColumn = QStringLiteral("applicationId");
const QString kPhraseColumn = QStringLiteral("phrase");
const QString kReplacementColumn = QStringLiteral("replacement");

// Keys no column names: the editor carries them from record to record so a
// value nobody can see survives an edit to one that everybody can.
const QString kLastUsedMsKey = QStringLiteral("lastUsedMs");
const QString kSourceIdKey = QStringLiteral("sourceId");
const QString kCorrectionIdKey = QStringLiteral("id");
const QString kCreatedAtKey = QStringLiteral("createdAtMs");
const QString kConfidenceKey = QStringLiteral("confidence");
const QString kEvidenceCountKey = QStringLiteral("evidenceCount");
const QString kLastObservedKey = QStringLiteral("lastObservedAtMs");
const QString kWhatsNewAction = QStringLiteral("whatsNew");
const QString kWhatsNewNotes = QStringLiteral("whatsNewNotes");

// The paste-method value a category row carries when it defers to the default
// paste, which is stored as the absence of a rule.
QString inheritGlobalPasteRule()
{
    return QStringLiteral("inherit");
}

// The paste chord differs per platform, and so does the copy that names it.
QList<RowOption> pasteMethodOptions(bool includeGlobalFallback)
{
    QList<RowOption> options;
    if (includeGlobalFallback) {
        options.append({inheritGlobalPasteRule(), QStringLiteral("Use the default paste")});
    }
#ifdef Q_OS_MACOS
    // Terminals on macOS take the same Cmd+V as every other app
    // (MacPasteDelivery::paste sends one chord), so there is no terminal
    // option; shownPasteMethod shows a stored one as Standard paste.
    options.append({pasteMethodName(PasteMethod::StandardPaste), QStringLiteral("Standard paste (Cmd+V)")});
    options.append({pasteMethodName(PasteMethod::DirectInsert),
                    QStringLiteral("Direct insertion (Accessibility)")});
#elif defined(Q_OS_WIN)
    options.append({pasteMethodName(PasteMethod::StandardPaste), QStringLiteral("Standard paste (Ctrl+V)")});
    options.append({pasteMethodName(PasteMethod::TerminalPaste),
                    QStringLiteral("Terminal paste (Ctrl+Shift+V)")});
    options.append({pasteMethodName(PasteMethod::DirectInsert),
                    QStringLiteral("Direct insertion (UI Automation)")});
#else
    options.append({pasteMethodName(PasteMethod::StandardPaste), QStringLiteral("Standard paste (Ctrl+V)")});
    options.append({pasteMethodName(PasteMethod::TerminalPaste),
                    QStringLiteral("Terminal paste (Ctrl+Shift+V)")});
    options.append({pasteMethodName(PasteMethod::DirectInsert),
                    QStringLiteral("Direct insertion (desktop accessibility)")});
#endif
    options.append({pasteMethodName(PasteMethod::ClipboardOnly), QStringLiteral("Clipboard only")});
    return options;
}

// A paste rule's method as its choice shows it. Terminal paste is Standard
// paste on macOS, so a stored terminal rule reads as the option that does the
// same, and stays as stored until someone picks another.
QString shownPasteMethod(PasteMethod method)
{
#ifdef Q_OS_MACOS
    if (method == PasteMethod::TerminalPaste) {
        return pasteMethodName(PasteMethod::StandardPaste);
    }
#endif
    return pasteMethodName(method);
}

QString applicationIdHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Use the app's bundle identifier, such as com.apple.Terminal.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Use the lowercase executable name without .exe, such as notepad.");
#else
    return QStringLiteral("Use the app's desktop ID, such as org.kde.konsole.");
#endif
}

QString applicationPasteRuleHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Override paste behavior for an exact bundle identifier, such as com.apple.Terminal.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Override paste behavior for an exact lowercase executable name, such as notepad.");
#else
    return QStringLiteral("Override paste behavior for an exact application ID, such as org.kde.konsole.");
#endif
}

QString targetAccessibilityHint()
{
    return accessibilityGateHelp(QStringLiteral("identify the target application"));
}

// The action the front end runs to lift an accessibility gate, and the row
// state that names it. Every row that needs a known target declares this.
const QString kEnableAccessibilityAction = QStringLiteral("enableAccessibility");

using Gate = std::function<bool(const AppSettings &, const Capabilities &)>;

// Holds a row until `open` says yes, on top of any gate it already has. The
// note names the gate added last when that one is closed, and otherwise
// whichever earlier gate is, so it never names a gate that is open.
void addGate(SettingsRow &row, Gate open, const QString &help)
{
    row.enabled = [open, existing = row.enabled](const AppSettings &settings,
                                                 const Capabilities &capabilities) {
        return open(settings, capabilities) && (!existing || existing(settings, capabilities));
    };
    row.disabledHelpValue = [open, help, existing = row.disabledHelpValue, fallback = row.disabledHelp](
                                const AppSettings &settings, const Capabilities &capabilities) {
        if (!open(settings, capabilities)) {
            return help;
        }
        return existing ? existing(settings, capabilities) : fallback;
    };
    if (row.disabledHelp.isEmpty()) {
        row.disabledHelp = help;
    }
}

void gateOnTargetAccessibility(SettingsRow &row, const QString &help)
{
    addGate(row,
            [](const AppSettings &, const Capabilities &capabilities) {
                return capabilities.targetAccessibility;
            },
            help);
    row.sharedGate = QStringLiteral("targetAccessibility");
    // Nothing in the app can make UI Automation available, so Windows offers
    // no action beside the note.
#ifndef Q_OS_WIN
    row.disabledAction = kEnableAccessibilityAction;
    row.disabledActionLabel = accessibilityGrantActionLabel();
#endif
}

bool refinementOn(const AppSettings &settings, const Capabilities &)
{
    return settings.refinement.providerId != QStringLiteral("none");
}

// Refinement provider "None" means no refinement runs, so every setting that
// only shapes a refinement request does nothing. Grey those rows out rather
// than letting them read as live choices.
void gateOnRefinementProvider(SettingsRow &row)
{
    addGate(row, refinementOn, QStringLiteral("Refinement is off."));
}

// The categories the Output page offers a paste rule for. A rule stored for any
// other category is left alone rather than dropped.
QList<AppCategory> managedPasteCategories()
{
    return {
        AppCategory::Terminal,
        AppCategory::Browser,
        AppCategory::Email,
        AppCategory::Office,
        AppCategory::CodeEditor,
        AppCategory::AiCoding,
        AppCategory::General,
    };
}

// A paste rule reads as a group of apps, where a recognition rule reads as one.
QString pasteCategoryLabel(AppCategory category)
{
    switch (category) {
    case AppCategory::Terminal:
        return QStringLiteral("Terminals");
    case AppCategory::Browser:
        return QStringLiteral("Browsers");
    case AppCategory::Email:
        return QStringLiteral("Email apps");
    case AppCategory::Office:
        return QStringLiteral("Office apps");
    case AppCategory::CodeEditor:
        return QStringLiteral("Code editors");
    case AppCategory::AiCoding:
        return QStringLiteral("AI coding apps");
    case AppCategory::General:
    case AppCategory::Unknown:
        break;
    }
    return QStringLiteral("Other apps");
}

void setPasteRule(QList<PasteRule> &rules,
                  PasteRuleScope scope,
                  const QString &match,
                  PasteMethod method)
{
    for (PasteRule &rule : rules) {
        if (rule.scope == scope && rule.match == match) {
            rule.method = method;
            rule.enabled = true;
            return;
        }
    }
    rules.append({scope, match, method, true});
}

void removePasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match)
{
    rules.removeIf([scope, &match](const PasteRule &rule) {
        return rule.scope == scope && rule.match == match;
    });
}

QList<RowOption> appCategoryOptions()
{
    QList<RowOption> options{{QString(), QStringLiteral("Automatic")}};
    for (AppCategory category : {AppCategory::General,
                                 AppCategory::Terminal,
                                 AppCategory::Browser,
                                 AppCategory::Email,
                                 AppCategory::Office,
                                 AppCategory::CodeEditor,
                                 AppCategory::AiCoding}) {
        options.append({appCategoryName(category), appCategoryLabel(category)});
    }
    return options;
}

QList<RowOption> writingProfileOptions(const AppSettings &settings)
{
    return QList<RowOption>{{QString(), QStringLiteral("Automatic")}}
        + writingProfileChoices(settings.refinement.writingProfiles);
}

Options fixedOptions(QList<RowOption> options)
{
    return [options = std::move(options)](const AppSettings &) { return options; };
}

SettingsRow choiceRow(QString id, QString label, QString help, Options options, Getter get, Setter set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Choice;
    row.options = std::move(options);
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toString());
    };
    return row;
}

SettingsRow toggleRow(QString id,
                      QString label,
                      QString help,
                      std::function<bool(const AppSettings &)> get,
                      std::function<void(AppSettings &, bool)> set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Toggle;
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toBool());
    };
    return row;
}

SettingsRow numberRow(QString id,
                      QString label,
                      QString help,
                      NumberRange range,
                      std::function<int(const AppSettings &)> get,
                      std::function<void(AppSettings &, int)> set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Number;
    row.range = std::move(range);
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toInt());
    };
    return row;
}

SettingsRow actionRow(QString id, QString label, QString help, QString actionLabel)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Action;
    row.actionLabel = std::move(actionLabel);
    return row;
}

SettingsRow customRow(QString id, QString label, QString help)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Custom;
    return row;
}

SettingsRow collectionRow(QString id, QString label, QString help, CollectionDescriptor collection)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Collection;
    collection.actions.append({QStringLiteral("undoDelete"), QStringLiteral("Undo delete")});
    row.value = [collection](const AppSettings &settings) {
        return QVariant::fromValue(collection.records(settings));
    };
    row.apply = [collection](AppSettings &settings, const QVariant &value) {
        collection.apply(settings, value.value<QList<QVariantMap>>());
    };
    row.collection = std::move(collection);
    return row;
}

SettingsRow infoRow(QString id, QString label, QString help, QString text)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Info;
    row.value = [text = std::move(text)](const AppSettings &) { return QVariant(text); };
    return row;
}

SettingsRow textRow(QString id, QString label, QString help, Getter get, Setter set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Text;
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toString().trimmed());
    };
    return row;
}

LiveFacts liveFacts(const SchemaContext &context)
{
    return context.liveFacts ? context.liveFacts() : LiveFacts{};
}

// Rows that only mean something while one provider is chosen sit under its
// picker and come and go with it.
std::function<bool(const AppSettings &, const Capabilities &)> whileSpeechProvider(const QString &id)
{
    return [id](const AppSettings &settings, const Capabilities &) {
        return settings.speech.providerId == id;
    };
}

std::function<bool(const AppSettings &, const Capabilities &)> whileRefinementProvider(const QString &id)
{
    return [id](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.providerId == id;
    };
}

QList<RowOption> namedOptions(const QStringList &ids)
{
    QList<RowOption> options;
    for (const QString &id : ids) {
        options.append({id, id});
    }
    return options;
}

// Test connection: the row's subtitle is the last verdict.
SettingsRow connectionTestRow(QString id, std::function<QString(const LiveFacts &)> status,
                              std::function<LiveFacts(const AppSettings &)> facts)
{
    const QString untested = QStringLiteral("Not tested yet.");
    SettingsRow row = actionRow(std::move(id), QStringLiteral("Connection"), untested,
                                QStringLiteral("Test connection"));
    row.helpValue = [status = std::move(status), facts = std::move(facts), untested](const AppSettings &settings) {
        const QString verdict = status(facts(settings));
        return verdict.isEmpty() ? untested : verdict;
    };
    return row;
}

// One line on which Local Runners answered the last look.
QString runnersSummary(const LiveFacts &live)
{
    if (live.detectingRunners) {
        return lookingForRunnersStatus();
    }
    if (live.runners.isEmpty()) {
        return QStringLiteral("No Ollama, LM Studio or llama-server is running on this computer.");
    }
    QStringList found;
    for (const RowOption &runner : live.runners) {
        found.append(runner.label);
    }
    return QStringLiteral("Running: %1.").arg(found.join(QStringLiteral(", ")));
}

// The speech Custom Endpoint, under the Service picker.
QList<SettingsRow> speechEndpointRows(const std::function<LiveFacts(const AppSettings &)> &facts)
{
    QList<SettingsRow> rows{
        textRow(QStringLiteral("speechEndpointUrl"),
                QStringLiteral("Server URL"),
                QStringLiteral("Any server with an OpenAI-style audio transcriptions API."),
                [](const AppSettings &settings) { return settings.speech.endpoint.baseUrl; },
                [](AppSettings &settings, const QString &value) { settings.speech.endpoint.baseUrl = value; }),
        textRow(QStringLiteral("speechEndpointPath"),
                QStringLiteral("Path"),
                QStringLiteral("whisper.cpp uses /inference."),
                [](const AppSettings &settings) { return settings.speech.endpoint.path; },
                [](AppSettings &settings, const QString &value) { settings.speech.endpoint.path = value; }),
        textRow(QStringLiteral("speechEndpointApiKey"),
                QStringLiteral("API key"),
                QStringLiteral("Optional. ") + keyStorageHelp(),
                [](const AppSettings &settings) { return settings.speech.endpoint.apiKey; },
                [](AppSettings &settings, const QString &value) { settings.speech.endpoint.apiKey = value; }),
        textRow(QStringLiteral("speechEndpointModel"),
                QStringLiteral("Model"),
                QStringLiteral("Test the connection to list the server's models, or type one."),
                [](const AppSettings &settings) { return settings.speech.endpoint.model; },
                [](AppSettings &settings, const QString &value) { settings.speech.endpoint.model = value; }),
        connectionTestRow(QStringLiteral("speechEndpointTest"),
                          [](const LiveFacts &live) { return live.speechEndpointStatus; }, facts),
    };
    rows[2].secret = true;
    rows[3].contentWidthHint = 20;
    rows[3].suggestions = [facts](const AppSettings &settings) {
        return namedOptions(facts(settings).speechEndpointModels);
    };
    for (SettingsRow &row : rows) {
        row.visible = whileSpeechProvider(QStringLiteral("endpoint"));
    }
    return rows;
}

// The Local Model dictation uses, under the Service picker. It is the
// setting "Use this model" writes, so the two always agree. With nothing
// downloaded there is nothing to choose, and the row sends people to the page
// that downloads.
QList<SettingsRow> speechLocalModelRows(const std::function<LiveFacts()> &facts)
{
    SettingsRow model = choiceRow(
        QStringLiteral("speechLocalModel"),
        QStringLiteral("Model"),
        QStringLiteral("Downloaded models. Get others on the %1 page.")
            .arg(paneTitle(QStringLiteral("localModels"))),
        [facts](const AppSettings &settings) {
            QList<RowOption> options;
            for (const QString &id : facts().downloadedModels) {
                if (const LocalModel *model = findLocalModel(id)) {
                    options.append({model->id, model->name, model->bestFor});
                }
            }
            const QString chosen = settings.speech.local.modelId;
            if (std::none_of(options.cbegin(), options.cend(),
                             [&chosen](const RowOption &option) { return option.id == chosen; })) {
                const LocalModel *missing = findLocalModel(chosen);
                options.append({chosen,
                                QStringLiteral("%1 (not downloaded)").arg(missing ? missing->name : chosen),
                                QString(),
                                false});
            }
            return options;
        },
        [](const AppSettings &settings) { return settings.speech.local.modelId; },
        [](AppSettings &settings, const QString &value) {
            settings.speech.local.modelId = value;
            settings.speech.local.modelChosen = true;
        });
    model.contentWidthHint = 24;

    SettingsRow download = actionRow(QStringLiteral("speechLocalModelDownload"),
                                     QStringLiteral("Model"),
                                     QStringLiteral("No model is downloaded yet. Download one to "
                                                    "dictate on this computer."),
                                     QStringLiteral("Open %1").arg(paneTitle(QStringLiteral("localModels"))));

    const auto whileLocal = whileSpeechProvider(QStringLiteral("local"));
    model.visible = [whileLocal, facts](const AppSettings &settings, const Capabilities &capabilities) {
        return whileLocal(settings, capabilities) && !facts().downloadedModels.isEmpty();
    };
    download.visible = [whileLocal, facts](const AppSettings &settings, const Capabilities &capabilities) {
        return whileLocal(settings, capabilities) && facts().downloadedModels.isEmpty();
    };
    return {std::move(model), std::move(download)};
}

// Refinement through a Local Runner, under the Provider picker.
QList<SettingsRow> localRunnerRows(const std::function<LiveFacts()> &facts)
{
    SettingsRow runner = choiceRow(
        QStringLiteral("localRunner"),
        QStringLiteral("Runner"),
        QStringLiteral("The app on this computer that runs the cleanup model."),
        [facts](const AppSettings &settings) {
            const LiveFacts live = facts();
            QList<RowOption> options = live.runners;
            const QString chosen = settings.refinement.localRunner.runner;
            if (std::none_of(options.cbegin(), options.cend(),
                             [&chosen](const RowOption &option) { return option.id == chosen; })) {
                options.append({chosen, chosen.isEmpty() ? QStringLiteral("Not selected") : live.detectingRunners
                                            ? localRunnerName(chosen)
                                            : QStringLiteral("%1 (not running)").arg(localRunnerName(chosen))});
            }
            return options;
        },
        [](const AppSettings &settings) { return settings.refinement.localRunner.runner; },
        [](AppSettings &settings, const QString &value) { settings.refinement.localRunner.runner = value; });
    runner.helpValue = [facts](const AppSettings &) {
        const LiveFacts live = facts();
        if (live.detectingRunners) {
            return lookingForRunnersStatus();
        }
        return live.runners.isEmpty()
            ? QStringLiteral("None found. Install and start Ollama, LM Studio or llama-server.")
            : QStringLiteral("The app on this computer that runs the cleanup model.");
    };

    SettingsRow model = textRow(
        QStringLiteral("localRunnerModel"),
        QStringLiteral("Model"),
        QStringLiteral("A cleanup model the runner has downloaded."),
        [](const AppSettings &settings) { return settings.refinement.localRunner.model; },
        [](AppSettings &settings, const QString &value) { settings.refinement.localRunner.model = value; });
    model.contentWidthHint = 20;
    model.suggestions = [facts](const AppSettings &settings) {
        return namedOptions(facts().runnerModels.value(settings.refinement.localRunner.runner));
    };

    SettingsRow detect = actionRow(QStringLiteral("localRunnerDetect"),
                                   QStringLiteral("Look for runners"),
                                   QStringLiteral("Not checked yet."),
                                   QStringLiteral("Look for runners again"));
    detect.helpValue = [facts](const AppSettings &) { return runnersSummary(facts()); };

    QList<SettingsRow> rows{std::move(runner), std::move(model), std::move(detect)};
    for (SettingsRow &row : rows) {
        row.visible = whileRefinementProvider(QStringLiteral("local"));
    }
    return rows;
}

// The refinement Custom Endpoint, under the Provider picker.
QList<SettingsRow> refinementEndpointRows(const std::function<LiveFacts(const AppSettings &)> &facts)
{
    QList<SettingsRow> rows{
        choiceRow(QStringLiteral("refinementEndpointServer"),
                  QStringLiteral("Server"),
                  QStringLiteral("Your own server, or the CLI Proxy API server set up under %1.")
                      .arg(paneTitle(QStringLiteral("accounts"))),
                  fixedOptions({
                      {QString(), QStringLiteral("Custom")},
                      {QStringLiteral("cliproxy"), QStringLiteral("CLI Proxy API")},
                  }),
                  [](const AppSettings &settings) { return settings.refinement.endpoint.preset; },
                  [](AppSettings &settings, const QString &value) { editRefinementEndpoint(settings, {.preset = value}); }),
        choiceRow(QStringLiteral("refinementEndpointFormat"),
                  QStringLiteral("Format"),
                  QStringLiteral("The API the server speaks."),
                  fixedOptions({
                      {QStringLiteral("openai"), QStringLiteral("OpenAI-compatible (Chat Completions)")},
                      // ui-lint: allow title-case: Messages is the name of Anthropic's API.
                      {QStringLiteral("anthropic"), QStringLiteral("Anthropic-compatible (Messages)")},
                  }),
                  [](const AppSettings &settings) { return resolvedRefinementEndpoint(settings.refinement).format; },
                  [](AppSettings &settings, const QString &value) { editRefinementEndpoint(settings, {.format = value}); }),
        textRow(QStringLiteral("refinementEndpointUrl"),
                QStringLiteral("Server URL"),
                QStringLiteral("The API base, such as http://localhost:11434/v1."),
                [](const AppSettings &settings) { return resolvedRefinementEndpoint(settings.refinement).apiBase; },
                [](AppSettings &settings, const QString &value) { editRefinementEndpoint(settings, {.baseUrl = value}); }),
        textRow(QStringLiteral("refinementEndpointApiKey"),
                QStringLiteral("API key"),
                QStringLiteral("Optional. ") + keyStorageHelp(),
                [](const AppSettings &settings) { return resolvedRefinementEndpoint(settings.refinement).apiKey; },
                [](AppSettings &settings, const QString &value) { editRefinementEndpoint(settings, {.apiKey = value}); }),
        textRow(QStringLiteral("refinementEndpointModel"),
                QStringLiteral("Model"),
                QStringLiteral("Test the connection to list the server's models, or type one."),
                [](const AppSettings &settings) { return resolvedRefinementEndpoint(settings.refinement).model; },
                [](AppSettings &settings, const QString &value) { editRefinementEndpoint(settings, {.model = value}); }),
        connectionTestRow(QStringLiteral("refinementEndpointTest"),
                          [](const LiveFacts &live) { return live.refinementEndpointStatus; }, facts),
    };
    rows[3].secret = true;
    rows[4].contentWidthHint = 20;
    rows[4].suggestions = [facts](const AppSettings &settings) {
        return namedOptions(facts(settings).refinementEndpointModels);
    };
    for (SettingsRow &row : rows) {
        row.visible = whileRefinementProvider(QStringLiteral("endpoint"));
    }
    return rows;
}

const QString kRepositoryUrl = QStringLiteral("https://github.com/firemonster612/speecher");

// The +g<sha> build metadata a nightly version carries; empty for a Stable
// Release or a version recorded before nightlies existed.
QString versionCommit(const QString &version)
{
    static const QRegularExpression sha(QStringLiteral("[+]g([0-9a-fA-F]+)$"));
    return sha.match(version).captured(1);
}

// One embedded history entry as a Markdown bullet. A merge commit's subject
// names only the branch, so its pull request title rides in on the body's
// first line; a squash merge already carries the title and "(#N)" in the
// subject. Anything else links the commit itself.
QString commitBullet(const QString &sha, const QString &subject, const QString &bodyTitle)
{
    static const QRegularExpression mergedPullRequest(
        QStringLiteral("^Merge pull request #(\\d+) from \\S+$"));
    static const QRegularExpression squashedPullRequest(
        QStringLiteral("^(.+) \\(#(\\d+)\\)$"));
    const auto pullRequestLink = [](const QString &number) {
        return QStringLiteral("[#%1](%2/pull/%1)").arg(number, kRepositoryUrl);
    };
    const QRegularExpressionMatch merged = mergedPullRequest.match(subject);
    if (merged.hasMatch()) {
        return QStringLiteral("- %1 (%2)").arg(bodyTitle.isEmpty() ? subject : bodyTitle,
                                               pullRequestLink(merged.captured(1)));
    }
    const QRegularExpressionMatch squashed = squashedPullRequest.match(subject);
    if (squashed.hasMatch()) {
        return QStringLiteral("- %1 (%2)").arg(squashed.captured(1),
                                               pullRequestLink(squashed.captured(2)));
    }
    return QStringLiteral("- %1 ([%2](%3/commit/%2))").arg(subject, sha, kRepositoryUrl);
}

QString embeddedReleaseHistory()
{
    QFile file(QStringLiteral(":/releases/history.log"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString releaseNotesMarkdown(const SchemaContext &context)
{
    static const bool resourceInitialized = [] {
        initializeReleaseNotesResource();
        return true;
    }();
    Q_UNUSED(resourceInitialized);
    struct Note {
        QString version;
        QString body;
    };
    QList<Note> notes;
    const QDir directory(QStringLiteral(":/releases"));
    for (const QString &fileName : directory.entryList({QStringLiteral("*.md")}, QDir::Files)) {
        QFile file(directory.filePath(fileName));
        // Text mode: a Windows checkout gives the bundled notes CRLF line ends.
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            notes.append({QFileInfo(fileName).completeBaseName(),
                          releaseNotesForThisPlatform(QString::fromUtf8(file.readAll()))});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const Note &left, const Note &right) {
        return compareBaseVersions(left.version, right.version) > 0;
    });

    const QString nightlyChanges = nightlyChangesMarkdown(embeddedReleaseHistory(),
                                                          context.lastSeenVersion,
                                                          context.currentVersion);

    QList<Note> selected;
    if (!context.lastSeenVersion.isEmpty()) {
        // A nightly's base is the last stable tag patch+1, so it predates the
        // Stable Release of its own base version. An update that moves past
        // that base crossed the release; its notes are news to the user even
        // though the base versions compare equal.
        const bool crossedOwnBase =
            context.lastSeenVersion.contains(QStringLiteral("-nightly"))
            && compareBaseVersions(context.currentVersion, context.lastSeenVersion) > 0;
        for (const Note &note : notes) {
            const int afterLastSeen = compareBaseVersions(note.version, context.lastSeenVersion);
            if ((afterLastSeen > 0 || (afterLastSeen == 0 && crossedOwnBase))
                && compareBaseVersions(note.version, context.currentVersion) <= 0) {
                selected.append(note);
            }
        }
    }
    // A nightly-to-nightly update that crosses no Stable Release reports its
    // own commits; only without those does the newest stable note stand in.
    if (selected.isEmpty() && nightlyChanges.isEmpty()) {
        const auto note = std::find_if(notes.cbegin(), notes.cend(), [&context](const Note &candidate) {
            return compareBaseVersions(candidate.version, context.currentVersion) <= 0;
        });
        if (note != notes.cend()) {
            selected.append(*note);
        }
    }

    QString markdown;
    if (!nightlyChanges.isEmpty()) {
        markdown = QStringLiteral("# Speecher %1\n\n%2").arg(context.currentVersion, nightlyChanges);
    }
    for (const Note &note : selected) {
        if (!markdown.isEmpty()) {
            markdown += QStringLiteral("\n\n---\n\n");
        }
        markdown += QStringLiteral("# Speecher %1\n\n%2").arg(note.version, note.body);
    }
    if (markdown.isEmpty()) {
        markdown = QStringLiteral("Release notes are not available in this build.");
    }
    if (nightlyChanges.isEmpty() && context.currentVersion.contains(QStringLiteral("-nightly"))) {
        markdown += QStringLiteral("\n\n[View releases](%1/releases)").arg(kRepositoryUrl);
    }
    return markdown;
}

// Where desktop accessibility stands, which setup asks for and paste rules,
// context and learning need: "On", or what is missing and the way to grant it.
// Windows has no switch for UI Automation, so it has no row.
QList<SettingsRow> accessibilityRows()
{
#ifdef Q_OS_WIN
    return {};
#else
#ifdef Q_OS_MACOS
    const QString label = QStringLiteral("Accessibility");
#else
    const QString label = QStringLiteral("Desktop accessibility");
#endif
    SettingsRow on = infoRow(QStringLiteral("desktopAccessibility"), label, QString(), QStringLiteral("On"));
    on.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.targetAccessibility;
    };
    // Named for the action it runs, which every front end already handles.
    SettingsRow off = actionRow(kEnableAccessibilityAction,
                                label,
                                QStringLiteral("Off. Speecher cannot tell which app you are in, so paste "
                                               "rules and context are limited."),
                                accessibilityGrantActionLabel());
    off.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return !capabilities.targetAccessibility;
    };
    return {std::move(on), std::move(off)};
#endif
}

SettingsPage generalPage(const SchemaContext &context)
{
    QList<SettingsRow> appRows;
#ifndef Q_OS_MACOS
    // macOS keeps appearance in System Settings, so Speecher follows it there.
    SettingsRow theme = choiceRow(
        QStringLiteral("themeControl"),
        QStringLiteral("Theme"),
        QString(),
        fixedOptions({
            {QStringLiteral("system"), QStringLiteral("System")},
            {QStringLiteral("light"), QStringLiteral("Light")},
            {QStringLiteral("dark"), QStringLiteral("Dark")},
        }),
        [](const AppSettings &settings) { return settings.ui.theme; },
        [](AppSettings &settings, const QString &value) { settings.ui.theme = value; });
    // A desktop that ignores the request would otherwise offer Light and Dark
    // as if they did something.
    theme.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.colorSchemeOverride;
    };
    theme.disabledHelp = QStringLiteral(
        "This desktop chooses the color scheme itself, so Speecher follows it.");
    appRows.append(std::move(theme));
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    appRows.append(toggleRow(
        QStringLiteral("launchAtLogin"),
        QStringLiteral("Start Speecher at login"),
        QStringLiteral("Dictation only works while Speecher is running."),
        [](const AppSettings &settings) { return settings.launchAtLogin; },
        [](AppSettings &settings, bool value) { settings.launchAtLogin = value; }));
    // The store writes the setting whether or not the computer honours it, so
    // without this the toggle would sit there saying yes to something that
    // never happens.
    SettingsRow launchAtLoginRefused =
        infoRow(QStringLiteral("launchAtLoginProblem"),
                QStringLiteral("Caution"),
                QString(),
                QStringLiteral("This computer did not accept the change, so Speecher may "
                               "not start at login. Turn it off and on again to retry."));
    launchAtLoginRefused.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return !capabilities.launchAtLoginAccepted;
    };
    appRows.append(std::move(launchAtLoginRefused));
#endif
    appRows.append(accessibilityRows());
    appRows.append(actionRow(QStringLiteral("runSetup"),
                             QStringLiteral("Setup assistant"),
                             QStringLiteral("Go through the first-run steps again."),
                             QStringLiteral("Run setup assistant")));

    // The recorder, which every front end draws with its own key capture.
    QList<SettingsRow> shortcutRows{customRow(
        QStringLiteral("globalShortcut"),
        QStringLiteral("Global Shortcut"),
        QStringLiteral("Start or stop dictation from anywhere."))};
    SettingsRow activationMode = choiceRow(
        QStringLiteral("activationMode"),
        QStringLiteral("Shortcut behavior"),
        QStringLiteral("What pressing the Global Shortcut does."),
        fixedOptions({
            {shortcutActivationModeName(ShortcutActivationMode::PushToTalk),
             QStringLiteral("Push to talk"),
             QStringLiteral("Dictate only while the key is held.")},
            {shortcutActivationModeName(ShortcutActivationMode::Toggle),
             QStringLiteral("Toggle"),
             QStringLiteral("One press starts, the next press stops.")},
            {shortcutActivationModeName(ShortcutActivationMode::Hybrid),
             QStringLiteral("Hybrid"),
             QStringLiteral("A tap toggles; holding dictates until release.")},
        }),
        [](const AppSettings &settings) {
            return shortcutActivationModeName(settings.shortcutActivationMode);
        },
        [](AppSettings &settings, const QString &value) {
            settings.shortcutActivationMode = shortcutActivationModeFromName(value);
        });
#ifdef Q_OS_LINUX
    // A desktop-registered combination and a manual desktop shortcut both only
    // ever say "pressed".
    activationMode.tooltip = QStringLiteral(
        "Where the desktop does not report the key going up, Push to talk and Hybrid "
        "behave as Toggle.");
#endif
    activationMode.sinceVersion = QStringLiteral("0.1.6");
    shortcutRows.append(activationMode);
    // No clipboard status row here: the Output page's Paste with choice says
    // how text is delivered, and a platform's "clipboard path" is not a setting.

    QList<SettingsSection> uninstall;
#ifdef Q_OS_LINUX
    // Undoing the per-user install is the app's job on Linux: there is no
    // package manager entry for an AppImage to fall back on.
    uninstall.append({QStringLiteral("Uninstall"),
                      QString(),
                      {actionRow(QStringLiteral("removeSpeecher"),
                                 QStringLiteral("Remove Speecher"),
                                 QStringLiteral("Removes the app menu entry, the speecher command, the "
                                                "icon and the Global Shortcut, and optionally your settings."),
                                 QStringLiteral("Remove Speecher…"))}});
#endif

    SettingsRow updateChannel = choiceRow(
        QStringLiteral("updateChannel"),
        QStringLiteral("Update channel"),
        QString(),
        fixedOptions({
            {QStringLiteral("stable"), QStringLiteral("Stable"), QStringLiteral("Hand-tested releases.")},
            {QStringLiteral("nightly"),
             QStringLiteral("Nightly"),
             QStringLiteral("Untested builds from every push to master.")},
        }),
        [](const AppSettings &settings) { return updateChannelName(settings.updates.channel); },
        [](AppSettings &settings, const QString &value) {
            settings.updates.channel = updateChannelFromName(value);
        });
    updateChannel.sinceVersion = QStringLiteral("0.1.0");
    SettingsRow autoCheck = toggleRow(
        QStringLiteral("autoCheckUpdates"),
        QStringLiteral("Check for updates automatically"),
        QString(),
        [](const AppSettings &settings) { return settings.updates.autoCheck; },
        [](AppSettings &settings, bool value) { settings.updates.autoCheck = value; });
    autoCheck.sinceVersion = QStringLiteral("0.1.0");
    SettingsRow checkInterval = choiceRow(
        QStringLiteral("updateCheckInterval"),
        QStringLiteral("Check frequency"),
        QString(),
        fixedOptions({
            {QStringLiteral("30"), QStringLiteral("Every 30 minutes"), QString()},
            {QStringLiteral("60"), QStringLiteral("Every hour"), QString()},
            {QStringLiteral("360"), QStringLiteral("Every 6 hours"), QString()},
            {QStringLiteral("1440"), QStringLiteral("Once a day"), QString()},
        }),
        [](const AppSettings &settings) {
            return QString::number(settings.updates.checkIntervalMinutes);
        },
        [](AppSettings &settings, const QString &value) {
            settings.updates.checkIntervalMinutes = value.toInt();
        });
    checkInterval.sinceVersion = QStringLiteral("0.1.5");
    checkInterval.visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.updates.autoCheck;
    };
    SettingsRow autoInstall = toggleRow(
        QStringLiteral("autoInstallUpdates"),
        QStringLiteral("Download and install updates automatically"),
#ifdef Q_OS_MACOS
        QStringLiteral("Sparkle downloads updates in the background and prompts when they are ready to install."),
#elif defined(Q_OS_WIN)
        QStringLiteral("Speecher downloads Windows updates in the background and prompts when they are ready to install."),
#else
        QStringLiteral("AppImage updates install in the background and take effect after restart."),
#endif
        [](const AppSettings &settings) { return settings.updates.autoInstall; },
        [](AppSettings &settings, bool value) { settings.updates.autoInstall = value; });
    autoInstall.sinceVersion = QStringLiteral("0.1.0");
    autoInstall.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.automaticUpdateDownloads;
    };

    SettingsRow insightsEnabled = toggleRow(
        QStringLiteral("insightsEnabled"),
        QStringLiteral("Keep insights about your dictation"),
        QStringLiteral("Word counts, times and app names, never text or audio. Stays on this computer."),
        [](const AppSettings &settings) { return settings.insightsEnabled; },
        [](AppSettings &settings, bool value) { settings.insightsEnabled = value; });
    insightsEnabled.sinceVersion = QStringLiteral("0.2.1");
    // Turning insights off stops recording; it does not delete what is kept.
    // The note is the row's title, so it reads from the leading edge like
    // every other row rather than filling the control slot.
    SettingsRow insightsOffNote =
        infoRow(QStringLiteral("insightsOffNote"),
                QStringLiteral("Nothing new is recorded while this is off. History you already "
                               "have stays until you clear it."),
                QString(),
                QString());
    insightsOffNote.visible = [](const AppSettings &settings, const Capabilities &) {
        return !settings.insightsEnabled;
    };

    // Tunes the two previews, so it shows only while one of them is on.
    SettingsRow previewWords = numberRow(
        QStringLiteral("previewWords"),
        QStringLiteral("Preview words"),
        QStringLiteral("How many of the latest words each preview shows."),
        {1, 40, 1, QString()},
        [](const AppSettings &settings) { return settings.ui.previewWords; },
        [](AppSettings &settings, int value) { settings.ui.previewWords = value; });
    previewWords.visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.ui.transcriptionPreviewEnabled || settings.ui.refinementPreviewEnabled;
    };

    SettingsPage page{
        QStringLiteral("general"),
        {
            {QStringLiteral("App"), QString(), std::move(appRows)},
            {QStringLiteral("Insights"),
             QString(),
             {
                 std::move(insightsEnabled),
                 std::move(insightsOffNote),
                 actionRow(QStringLiteral("clearInsights"),
                           QStringLiteral("Insights history"),
                           QStringLiteral("Delete every recorded dictation from this computer."),
                           QStringLiteral("Clear insights history…")),
             }},
            {QStringLiteral("Shortcut"), QString(), std::move(shortcutRows)},
            {QStringLiteral("While dictating"),
             QString(),
             {
                 toggleRow(QStringLiteral("pauseMedia"),
                           QStringLiteral("Pause media while dictating"),
                           QString(),
                           [](const AppSettings &settings) { return settings.ui.pauseMediaDuringTranscription; },
                           [](AppSettings &settings, bool value) { settings.ui.pauseMediaDuringTranscription = value; }),
                 toggleRow(QStringLiteral("soundsEnabled"),
                           QStringLiteral("Play sounds when dictation starts and stops"),
                           QString(),
                           [](const AppSettings &settings) { return settings.ui.soundsEnabled; },
                           [](AppSettings &settings, bool value) { settings.ui.soundsEnabled = value; }),
                 toggleRow(QStringLiteral("transcriptionPreviewEnabled"),
                           QStringLiteral("Show live text while you speak"),
                           QString(),
                           [](const AppSettings &settings) { return settings.ui.transcriptionPreviewEnabled; },
                           [](AppSettings &settings, bool value) { settings.ui.transcriptionPreviewEnabled = value; }),
                 toggleRow(QStringLiteral("refinementPreviewEnabled"),
                           QStringLiteral("Show live text during refinement"),
                           QString(),
                           [](const AppSettings &settings) { return settings.ui.refinementPreviewEnabled; },
                           [](AppSettings &settings, bool value) { settings.ui.refinementPreviewEnabled = value; }),
                 std::move(previewWords),
                 numberRow(QStringLiteral("completionStatusDuration"),
                           QStringLiteral("Show the result for"),
                           QStringLiteral("How long the popup shows where the text went."),
                           {0, 5000, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.output.completionStatusDurationMs; },
                           [](AppSettings &settings, int value) {
                               settings.output.completionStatusDurationMs = value;
                           }),
             }},
            {QStringLiteral("Updates"),
             QString(),
             {
                 std::move(updateChannel),
                 std::move(autoCheck),
                 std::move(checkInterval),
                 std::move(autoInstall),
                 actionRow(QStringLiteral("checkForUpdates"),
                           QStringLiteral("Check for updates"),
                           QStringLiteral("Check the selected Update Channel for a newer build."),
                           QStringLiteral("Check now")),
                 infoRow(QStringLiteral("currentVersion"),
                         QStringLiteral("Current version"),
                         QString(),
                         context.currentVersion.isEmpty()
                             ? QStringLiteral("Unknown")
                             : context.currentVersion),
                 actionRow(kWhatsNewAction,
                           // ui-lint: allow title-case: names the What's New Page.
                           QStringLiteral("What's New"),
                           QStringLiteral("Release notes for this version, and the settings it added."),
                           QStringLiteral("Open")),
             }},
        },
    };
    page.sections.append(uninstall);
    return page;
}

SettingsPage whatsNewPage(const QList<SettingsPage> &pages, const SchemaContext &context)
{
    QList<SettingsRow> newRows;
    if (!context.lastSeenVersion.isEmpty()) {
        for (const SettingsPage &page : pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    // Custom rows need a page-specific factory, which What's New does not have.
                    if (row.kind != RowKind::Custom
                        && !row.sinceVersion.isEmpty()
                        && compareBaseVersions(row.sinceVersion, context.lastSeenVersion) > 0
                        && compareBaseVersions(row.sinceVersion, context.currentVersion) <= 0) {
                        newRows.append(row);
                    }
                }
            }
        }
    }

    // The page title already says what these are.
    SettingsRow notes = customRow(kWhatsNewNotes, QString(), QString());
    const QString markdown = releaseNotesMarkdown(context);
    notes.value = [markdown](const AppSettings &) { return QVariant(markdown); };
    QList<SettingsSection> sections{{QString(), QString(), {std::move(notes)}}};
    if (!newRows.isEmpty()) {
        sections.append({QStringLiteral("Try the new settings"), QString(), std::move(newRows)});
    }
    return {QStringLiteral("whatsNew"),
            std::move(sections)};
}

SettingsPage audioPage(const SchemaContext &context)
{
    SettingsRow speechProvider = choiceRow(
        QStringLiteral("speechProvider"),
        QStringLiteral("Service"),
        QStringLiteral("Service used to turn speech into a raw transcript."),
        fixedOptions(context.speechProviders),
        [](const AppSettings &settings) { return settings.speech.providerId; },
        [](AppSettings &settings, const QString &value) { settings.speech.providerId = value; });
    speechProvider.contentWidthHint = 24;
    // The subtitle explains the engine behind the selected service; the
    // per-provider text rides in on RowOption::help from the registry.
    const QList<RowOption> speechChoices = context.speechProviders;
    speechProvider.helpValue = [speechChoices](const AppSettings &settings) {
        for (const RowOption &option : speechChoices) {
            if (option.id == settings.speech.providerId && !option.help.isEmpty()) {
                return option.help;
            }
        }
        return QStringLiteral("Service used to turn speech into a raw transcript.");
    };

    SettingsRow finalRetranscribe = toggleRow(
        QStringLiteral("codexFinalRetranscribe"),
        QStringLiteral("Transcribe again for accuracy"),
        QStringLiteral("Adds one to five seconds after you stop."),
        [](const AppSettings &settings) { return settings.speech.codexFinalRetranscribe; },
        [](AppSettings &settings, bool value) { settings.speech.codexFinalRetranscribe = value; });
    finalRetranscribe.tooltip = QStringLiteral(
        "The live preview is unchanged; a second, whole-recording transcription fixes words "
        "the live pass misheard. It uses one extra ChatGPT request. Dictations longer than "
        "about a minute and a half keep the live transcript.");
    finalRetranscribe.sinceVersion = QStringLiteral("0.1.6");
    finalRetranscribe.visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.speech.providerId == QStringLiteral("codex");
    };

    SettingsRow device = choiceRow(
        QStringLiteral("audioDevice"),
        QStringLiteral("Input device"),
        QStringLiteral("The microphone Speecher records from."),
        [lister = context.audioInputDevices](const AppSettings &settings) {
            return audioDeviceOptions(lister ? lister() : QList<RowOption>(), settings.audio.deviceId);
        },
        [](const AppSettings &settings) { return settings.audio.deviceId; },
        [](AppSettings &settings, const QString &value) { settings.audio.deviceId = value; });
    device.contentWidthHint = 28;
    device.expensive = true;
    device.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.audioInput;
    };
    // What stands between Speecher and a microphone, and where to fix it.
#ifdef Q_OS_WIN
    device.disabledHelp = QStringLiteral("No microphone is available. Check that one is connected and "
                                         "that Windows lets desktop apps use it.");
    device.disabledAction = QStringLiteral("openMicrophoneSettings");
    device.disabledActionLabel = QStringLiteral("Open microphone settings");
#elif defined(Q_OS_MACOS)
    device.disabledHelp = QStringLiteral("No microphone is available. Check that one is connected and "
                                         "that Speecher may use it in Privacy & Security.");
    device.disabledAction = QStringLiteral("openMicrophoneSettings");
    device.disabledActionLabel = QStringLiteral("Open microphone settings");
#else
    device.disabledHelp = QStringLiteral("Speecher can't see a microphone. Plug one in or check that "
                                         "your sound server allows access.");
    device.disabledAction = QStringLiteral("refreshMicrophones");
    device.disabledActionLabel = QStringLiteral("Check again");
#endif

    // Each front end draws the level as its native meter beside a button
    // captioned by microphoneTestCaption().
    SettingsRow microphoneTest = customRow(
        QStringLiteral("microphoneTest"),
        QStringLiteral("Test microphone"),
        QStringLiteral("Speak and watch the level to check that the input device hears you."));
    microphoneTest.enabled = device.enabled;

    SettingsRow captureMode = choiceRow(
        QStringLiteral("captureMode"),
        QStringLiteral("Keep microphone open"),
        QStringLiteral("Keeping it open starts dictation a little faster, but the microphone "
                       "shows as in use the whole time."),
        fixedOptions({
            {QStringLiteral("on_demand"), QStringLiteral("Only while dictating")},
            {QStringLiteral("warm"), QStringLiteral("Between dictations too")},
        }),
        [](const AppSettings &settings) { return settings.audio.mode; },
        [](AppSettings &settings, const QString &value) { settings.audio.mode = value; });

    SettingsRow vadEnabled = toggleRow(
        QStringLiteral("vadEnabled"),
        QStringLiteral("Skip silence"),
        QStringLiteral("Leaves quiet stretches out of what is transcribed."),
        [](const AppSettings &settings) { return settings.audio.vadEnabled; },
        [](AppSettings &settings, bool value) { settings.audio.vadEnabled = value; });

    // Tunes Skip silence, so it shows only while that is on.
    SettingsRow vadThreshold = numberRow(
        QStringLiteral("vadThresholdPercent"),
        QStringLiteral("Quiet below"),
        QStringLiteral("Raise this if background noise is being kept; lower it if soft speech is "
                       "being cut."),
        {1, 20, 1, QStringLiteral("%")},
        [](const AppSettings &settings) { return settings.audio.vadThresholdPercent; },
        [](AppSettings &settings, int value) { settings.audio.vadThresholdPercent = value; });
    vadThreshold.visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.audio.vadEnabled;
    };

    return {
        QStringLiteral("audio"),
        {
            {QStringLiteral("Transcription"),
             QString(),
             QList<SettingsRow>{std::move(speechProvider), std::move(finalRetranscribe)}
                 + speechLocalModelRows([context] { return liveFacts(context); })
                 + speechEndpointRows([context](const AppSettings &draft) {
                     return context.liveFactsForDraft ? context.liveFactsForDraft(draft) : liveFacts(context);
                 })},
            {QStringLiteral("Microphone"),
             QString(),
             {
                 std::move(device),
                 std::move(microphoneTest),
                 std::move(captureMode),
                 numberRow(QStringLiteral("readinessTimeoutMs"),
                           QStringLiteral("Wait for microphone"),
                           QStringLiteral("How long to wait for the microphone to deliver sound before giving up."),
                           {500, 3000, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.readinessTimeoutMs; },
                           [](AppSettings &settings, int value) { settings.audio.readinessTimeoutMs = value; }),
             }},
            // The labels say what a change does to the recording rather than
            // how the pipeline works.
            {QStringLiteral("Recording"),
             QString(),
             {
                 std::move(vadEnabled),
                 std::move(vadThreshold),
                 numberRow(QStringLiteral("preRollMs"),
                           QStringLiteral("Keep before speech"),
                           QStringLiteral("Audio kept from just before you start, so the first word is not clipped."),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.preRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.preRollMs = value; }),
                 numberRow(QStringLiteral("postRollMs"),
                           QStringLiteral("Keep after stopping"),
                           QStringLiteral("Audio kept after you stop or go quiet, so the last word is not clipped."),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.postRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.postRollMs = value; }),
             }},
        },
    };
}

bool offers(const QList<RowOption> &options, const QString &id)
{
    return std::any_of(options.cbegin(), options.cend(),
                       [&id](const RowOption &option) { return option.id == id; });
}

// What a custom tone or level record holds besides its columns.
const QString kChoiceIdKey = QStringLiteral("id");
const QString kChoiceNameColumn = QStringLiteral("name");
const QString kToneInstructionColumn = QStringLiteral("instruction");
const QString kLevelBaseColumn = QStringLiteral("base");
const QString kLevelInstructionsColumn = QStringLiteral("instructions");

// The id a record already has, or a new one made from its name. A record with
// no name yet gets none, so the id comes from the name it is given.
QString recordChoiceId(const QVariantMap &record, QStringList &taken)
{
    QString id = record.value(kChoiceIdKey).toString();
    const QString name = record.value(kChoiceNameColumn).toString().trimmed();
    if (id.isEmpty() && !name.isEmpty()) {
        id = customChoiceId(name, taken);
        taken << id;
    }
    return id;
}

QStringList takenChoiceIds(const QList<QVariantMap> &records)
{
    QStringList ids;
    for (const QVariantMap &record : records) {
        ids << record.value(kChoiceIdKey).toString();
    }
    return ids;
}

// Blank and repeated names, built-in ones included, since both would show as
// the same entry in a profile's choices.
QStringList choiceNameProblems(const QList<QVariantMap> &records,
                               const QList<RowOption> &builtIns,
                               const QString &noun)
{
    QStringList problems;
    QStringList names;
    for (const RowOption &option : builtIns) {
        names << option.label.toCaseFolded();
    }
    for (const QVariantMap &record : records) {
        const QString name = record.value(kChoiceNameColumn).toString().trimmed();
        if (name.isEmpty()) {
            problems << QStringLiteral("Every %1 needs a name.").arg(noun);
        } else if (names.contains(name.toCaseFolded())) {
            problems << QStringLiteral("There is already a %1 named %2.").arg(noun, name);
        }
        names << name.toCaseFolded();
    }
    return problems;
}

// How many of something the person added, after what adding one does.
QString withCount(const QString &help, qsizetype count)
{
    return count == 0 ? help : QStringLiteral("%1 You have %2.").arg(help).arg(count);
}

// The tones a person added. The built-in ones are not listed: each one's
// instruction shows where a profile picks its tone.
SettingsRow customTonesRow()
{
    CollectionDescriptor tones;
    tones.columns = {
        {kChoiceNameColumn, QStringLiteral("Name"), ColumnKind::Text},
        {kToneInstructionColumn, QStringLiteral("Instruction"), ColumnKind::Text, {}, true},
    };
    tones.columns.last().multiline = true;
    tones.columns.last().placeholder = QStringLiteral("Short sentences, no exclamation marks.");
    tones.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const CustomTone &tone : settings.refinement.customTones) {
            records.append({{kChoiceIdKey, tone.id},
                            {kChoiceNameColumn, tone.name},
                            {kToneInstructionColumn, tone.instruction}});
        }
        return records;
    };
    tones.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QStringList taken = takenChoiceIds(records);
        QList<CustomTone> custom;
        for (const QVariantMap &record : records) {
            custom.append({recordChoiceId(record, taken),
                           record.value(kChoiceNameColumn).toString().trimmed(),
                           record.value(kToneInstructionColumn).toString()});
        }
        settings.refinement.customTones = custom;
    };
    tones.validate = [](const QList<QVariantMap> &records) {
        QStringList problems =
            choiceNameProblems(records, writingTones({}), QStringLiteral("tone"));
        for (const QVariantMap &record : records) {
            if (record.value(kToneInstructionColumn).toString().trimmed().isEmpty()) {
                problems << QStringLiteral("Every tone needs an instruction.");
            }
        }
        problems.removeDuplicates();
        return problems;
    };
    tones.blankRecord = {{kChoiceNameColumn, QString()}, {kToneInstructionColumn, QString()}};
    tones.addLabel = QStringLiteral("Add tone");
    tones.addDialogTitle = QStringLiteral("New tone");
    tones.emptyTitle = QStringLiteral("No tones of your own yet.");
    tones.minimumHeight = 220;

    const QString help = QStringLiteral("Add a voice you can pick in any profile.");
    SettingsRow row = collectionRow(QStringLiteral("customTones"), QString(), help, std::move(tones));
    row.dialog = {QStringLiteral("Your tones"), [help](const AppSettings &settings) {
                      return withCount(help, settings.refinement.customTones.size());
                  }};
    gateOnRefinementProvider(row);
    return row;
}

// The cleanup levels a person added, each built on a built-in one.
SettingsRow customCleanupLevelsRow()
{
    const QList<RowOption> bases = cleanupStrengths({}).mid(1)
        + QList<RowOption>{{kCustomOnlyCleanupBase, QStringLiteral("Custom only"),
                            QStringLiteral("Only the rules every level shares, such as keeping the facts.")}};
    CollectionDescriptor levels;
    levels.columns = {
        {kChoiceNameColumn, QStringLiteral("Name"), ColumnKind::Text},
        {kLevelBaseColumn, QStringLiteral("Based on"), ColumnKind::Choice, fixedOptions(bases)},
        {kLevelInstructionsColumn, QStringLiteral("Instructions"), ColumnKind::Text, {}, true},
    };
    levels.columns[1].dialogOnly = true;
    levels.columns.last().multiline = true;
    levels.columns.last().placeholder = QStringLiteral("Keep bullet points as bullet points.");
    levels.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const CustomCleanupLevel &level : settings.refinement.customCleanupLevels) {
            records.append({{kChoiceIdKey, level.id},
                            {kChoiceNameColumn, level.name},
                            {kLevelBaseColumn, level.base},
                            {kLevelInstructionsColumn, level.instructions}});
        }
        return records;
    };
    levels.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QStringList taken = takenChoiceIds(records);
        QList<CustomCleanupLevel> custom;
        for (const QVariantMap &record : records) {
            custom.append({recordChoiceId(record, taken),
                           record.value(kChoiceNameColumn).toString().trimmed(),
                           record.value(kLevelBaseColumn).toString(),
                           record.value(kLevelInstructionsColumn).toString()});
        }
        settings.refinement.customCleanupLevels = custom;
    };
    levels.validate = [](const QList<QVariantMap> &records) {
        QStringList problems = choiceNameProblems(
            records, cleanupStrengths({}), QStringLiteral("cleanup level"));
        for (const QVariantMap &record : records) {
            if (record.value(kLevelBaseColumn).toString() == kCustomOnlyCleanupBase
                && record.value(kLevelInstructionsColumn).toString().trimmed().isEmpty()) {
                problems << QStringLiteral("A Custom only cleanup level needs instructions.");
            }
        }
        problems.removeDuplicates();
        return problems;
    };
    levels.blankRecord = {{kChoiceNameColumn, QString()},
                          {kLevelBaseColumn, QStringLiteral("balanced")},
                          {kLevelInstructionsColumn, QString()}};
    levels.addLabel = QStringLiteral("Add cleanup level");
    levels.addDialogTitle = QStringLiteral("New cleanup level");
    levels.emptyTitle = QStringLiteral("No cleanup levels of your own yet.");
    levels.minimumHeight = 220;

    const QString help = QStringLiteral("A level built on Light, Medium or High, plus your own instructions.");
    SettingsRow row =
        collectionRow(QStringLiteral("customCleanupLevels"), QString(), help, std::move(levels));
    row.dialog = {QStringLiteral("Your cleanup levels"), [help](const AppSettings &settings) {
                      return withCount(help, settings.refinement.customCleanupLevels.size());
                  }};
    gateOnRefinementProvider(row);
    return row;
}

// Each refinement account's Model, Effort and Speed, shown under the Provider
// picker while that provider is chosen. Defined with the accounts below.
QList<SettingsRow> providerModelRows();

SettingsPage refinementPage(const SchemaContext &context)
{
    QList<RowOption> refiners;
    for (const RefinementProvider &provider : context.refinementProviders) {
        refiners.append({provider.id, provider.label});
    }
    refiners.append({QStringLiteral("none"), QStringLiteral("None")});

    // Context only shapes a refinement request, so it goes with refinement.
    SettingsRow targetContext = toggleRow(
        QStringLiteral("targetContextControl"),
        QStringLiteral("Text around your cursor"),
        QStringLiteral("Lets cleanup fit what you are writing."),
        [](const AppSettings &settings) { return settings.refinement.useTargetContext; },
        [](AppSettings &settings, bool value) { settings.refinement.useTargetContext = value; });
    targetContext.visible = refinementOn;
    gateOnTargetAccessibility(targetContext,
                              accessibilityGateHelp(QStringLiteral("send the app's text")));

    SettingsRow screenshots = toggleRow(
        QStringLiteral("includeScreenshotContext"),
        QStringLiteral("A screenshot"),
#ifdef Q_OS_MACOS
        QStringLiteral("Needs Screen Recording permission. Kept only for the current dictation."),
#else
        QStringLiteral("Kept only for the current dictation."),
#endif
        [](const AppSettings &settings) { return settings.refinement.includeScreenshotContext; },
        [](AppSettings &settings, bool value) { settings.refinement.includeScreenshotContext = value; });
    screenshots.visible = refinementOn;
    screenshots.disabledHelp = QStringLiteral("Only OpenAI and Anthropic refinement can use screenshots.");
    screenshots.enabled = [providers = context.refinementProviders](const AppSettings &settings,
                                                                   const Capabilities &) {
        for (const RefinementProvider &provider : providers) {
            if (provider.id == settings.refinement.providerId) {
                return provider.supportsScreenshotContext;
            }
        }
        return false;
    };

    const std::function<LiveFacts()> facts = [context] { return liveFacts(context); };
    return {
        QStringLiteral("refinement"),
        {
            {QStringLiteral("Provider"),
             QString(),
             QList<SettingsRow>{
                 choiceRow(QStringLiteral("refinementProvider"),
                           QStringLiteral("Provider"),
                           QStringLiteral("The service that cleans up your text."),
                           fixedOptions(refiners),
                           [](const AppSettings &settings) { return settings.refinement.providerId; },
                           [](AppSettings &settings, const QString &value) { settings.refinement.providerId = value; }),
             }
                 + providerModelRows()
                 + localRunnerRows(facts)
                 + refinementEndpointRows([context](const AppSettings &draft) {
                     return context.liveFactsForDraft ? context.liveFactsForDraft(draft) : liveFacts(context);
                 })},
            {QStringLiteral("What refinement can see"), QString(), {std::move(targetContext), std::move(screenshots)}},
        },
    };
}

// How refinement rewrites: the Writing Profiles, then what they choose from,
// which few people change, behind a button row each.
SettingsPage writingProfilesPage(const SchemaContext &context)
{
    SettingsRow fallbackProfile = choiceRow(
        QStringLiteral("defaultWritingProfile"),
        QStringLiteral("When the app isn't recognized"),
        QStringLiteral("Apps are matched to profiles under %1.").arg(paneTitle(QStringLiteral("output"))),
        [](const AppSettings &settings) { return writingProfileChoices(settings.refinement.writingProfiles); },
        [](const AppSettings &settings) {
            return offeredWritingProfile(settings.refinement.defaultWritingProfile,
                                         settings.refinement.writingProfiles);
        },
        [](AppSettings &settings, const QString &value) {
            settings.refinement.defaultWritingProfile = value;
        });
    gateOnRefinementProvider(fallbackProfile);

    SettingsRow profileBehavior;
    profileBehavior.id = QStringLiteral("writingProfileBehavior");
    profileBehavior.label = QStringLiteral("Profile behavior");
    profileBehavior.help = QStringLiteral(
        "Choose a Cleanup Level, a Tone and optional instructions for each profile. An output "
        "language translates what you say, with at least Light cleanup.");
    profileBehavior.kind = RowKind::Custom;
    profileBehavior.collection = writingProfileGrid();
    profileBehavior.value = [](const AppSettings &settings) {
        return QVariant::fromValue(settings.refinement.writingProfiles);
    };
    profileBehavior.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.writingProfiles = withCustomProfileIds(value.value<QList<WritingProfileSettings>>());
    };
    gateOnRefinementProvider(profileBehavior);

    // The dialog's title names it, so the field needs no label of its own.
    SettingsRow additionalInstructions = textRow(
        QStringLiteral("additionalInstructions"),
        QString(),
        QStringLiteral("Added to every refinement, before each profile's own instructions."),
        [](const AppSettings &settings) { return settings.refinement.additionalInstructions; },
        [](AppSettings &settings, const QString &value) {
            settings.refinement.additionalInstructions = value;
        });
    additionalInstructions.multiline = true;
    additionalInstructions.placeholder = QStringLiteral("Write numbers as digits.");
    additionalInstructions.dialog = {
        QStringLiteral("Instructions for every profile"), [](const AppSettings &settings) {
            const QString first = settings.refinement.additionalInstructions.trimmed().section(QLatin1Char('\n'), 0, 0);
            return first.isEmpty() ? QStringLiteral("None") : first;
        }};
    gateOnRefinementProvider(additionalInstructions);

    SettingsRow customPromptEnabled = toggleRow(
        QStringLiteral("customSystemPromptEnabled"),
        // ui-lint: allow avoid-term (the setting that replaces the system prompt)
        QStringLiteral("Use a custom system prompt"),
        QStringLiteral("Replaces the built-in rules with your prompt. Each profile's tone and "
                       "instructions still apply, and so do the instructions of a Cleanup Level "
                       "you added. A profile set to None is not refined unless it translates."),
        [](const AppSettings &settings) { return settings.refinement.customSystemPromptEnabled; },
        [](AppSettings &settings, bool value) { settings.refinement.customSystemPromptEnabled = value; });
    gateOnRefinementProvider(customPromptEnabled);
    SettingsRow resetCustomPrompt = actionRow(
        QStringLiteral("resetCustomSystemPrompt"),
        QStringLiteral("Built-in prompt"),
        QStringLiteral("Replace the prompt with the built-in one, at Medium cleanup with no tone."),
        QStringLiteral("Reset to built-in"));
    SettingsRow customPrompt = textRow(
        QStringLiteral("customSystemPrompt"),
        QStringLiteral("Prompt"),
        QStringLiteral("Selection editing and local models keep their own prompts."),
        [builtIn = context.builtInSystemPrompt](const AppSettings &settings) {
            const QString &stored = settings.refinement.customSystemPrompt;
            return stored.isEmpty() ? builtIn : stored;
        },
        [](AppSettings &settings, const QString &value) {
            settings.refinement.customSystemPrompt = value;
        });
    customPrompt.multiline = true;
    // One note above both, since they open and close together.
    for (SettingsRow *row : {&resetCustomPrompt, &customPrompt}) {
        row->groupId = QStringLiteral("customSystemPrompt");
        addGate(*row,
                [](const AppSettings &settings, const Capabilities &) {
                    return settings.refinement.customSystemPromptEnabled;
                },
                // ui-lint: allow avoid-term (the setting that replaces the system prompt)
                QStringLiteral("Turn on the custom system prompt to edit it."));
        gateOnRefinementProvider(*row);
    }
    // ui-lint: allow avoid-term (the setting that replaces the system prompt)
    const RowDialog promptDialog{QStringLiteral("Custom system prompt"), [](const AppSettings &settings) {
                                     return settings.refinement.customSystemPromptEnabled ? QStringLiteral("On")
                                                                                          : QStringLiteral("Off");
                                 }};
    for (SettingsRow *row : {&customPromptEnabled, &resetCustomPrompt, &customPrompt}) {
        row->dialog = promptDialog;
    }

    return {
        QStringLiteral("writingProfiles"),
        {
            {QStringLiteral("Profiles"), QString(), {std::move(fallbackProfile), std::move(profileBehavior)}},
            {QStringLiteral("Advanced"),
             QString(),
             {customTonesRow(), customCleanupLevelsRow(), std::move(additionalInstructions),
              std::move(customPromptEnabled), std::move(resetCustomPrompt), std::move(customPrompt)}},
        },
    };
}

// Downloads, the Speed Test and the model choice live in the list and detail
// custom row "localModelBrowser", which each front end supplies over
// LocalModelCatalog, LocalModelStore and LocalSetup. The rest is plain rows.
SettingsPage localModelsPage(const SchemaContext &context)
{
    const std::function<LiveFacts()> facts = [context] { return liveFacts(context); };

    // The model dictation uses, empty while it uses another provider; choosing
    // one switches transcription to it.
    SettingsRow browser = customRow(QStringLiteral("localModelBrowser"),
                                    QStringLiteral("Speech models"),
                                    QString());
    browser.value = [](const AppSettings &settings) {
        return QVariant(settings.speech.providerId == QStringLiteral("local")
                            ? settings.speech.local.modelId
                            : QString());
    };
    browser.apply = [](AppSettings &settings, const QVariant &value) {
        const QString modelId = value.toString();
        if (modelId.isEmpty()) {
            return;
        }
        settings.speech.local.modelId = modelId;
        settings.speech.local.modelChosen = true;
        settings.speech.providerId = QStringLiteral("local");
    };

    SettingsRow idleUnload = choiceRow(
        QStringLiteral("localIdleUnload"),
        QStringLiteral("Unload the model when idle"),
        QStringLiteral("Frees memory. The next dictation loads it again while you speak."),
        fixedOptions({
            {QStringLiteral("1"), QStringLiteral("After 1 minute")},
            {QStringLiteral("10"), QStringLiteral("After 10 minutes")},
            {QStringLiteral("60"), QStringLiteral("After 1 hour")},
            {QStringLiteral("0"), QStringLiteral("Never")},
        }),
        [](const AppSettings &settings) { return QString::number(settings.speech.local.idleUnloadMinutes); },
        [](AppSettings &settings, const QString &value) {
            settings.speech.local.idleUnloadMinutes = value.toInt();
        });

    // Its description names where the loaded model actually runs, which
    // Automatic leaves to transcribe.cpp.
    SettingsRow acceleration = choiceRow(
        QStringLiteral("localAcceleration"),
        QStringLiteral("Acceleration"),
        QStringLiteral("Automatic picks the fastest graphics card and falls back to the CPU."),
        [facts](const AppSettings &settings) {
            return localAccelerationOptions(facts().localGpus, settings.speech.local.runsOn);
        },
        [](const AppSettings &settings) { return settings.speech.local.runsOn.backend; },
        [facts](AppSettings &settings, const QString &value) {
            // A new backend starts on its first card; Automatic and the CPU
            // pick none.
            LocalRunsOn runsOn{value, QString()};
            const QList<RowOption> cards = localGraphicsCardOptions(facts().localGpus, runsOn);
            runsOn.deviceId = cards.isEmpty() ? QString() : cards.first().id;
            settings.speech.local.runsOn = runsOn;
        });
    acceleration.helpValue = [facts, help = acceleration.help](const AppSettings &) {
        const QString running = facts().localModelRunsOn;
        return running.isEmpty() ? help
                                 : help + QStringLiteral(" The loaded model is running on %1.").arg(running);
    };

    // Only worth asking when the chosen backend reaches more than one card,
    // or to show the card an older Automatic choice is pinned to.
    SettingsRow graphicsCard = choiceRow(
        QStringLiteral("localGraphicsCard"),
        QStringLiteral("Graphics card"),
        QStringLiteral("The card this acceleration runs on."),
        [facts](const AppSettings &settings) {
            return localGraphicsCardOptions(facts().localGpus, settings.speech.local.runsOn);
        },
        [](const AppSettings &settings) { return settings.speech.local.runsOn.deviceId; },
        [](AppSettings &settings, const QString &value) { settings.speech.local.runsOn.deviceId = value; });
    graphicsCard.visible = [facts](const AppSettings &settings, const Capabilities &) {
        const LocalRunsOn &runsOn = settings.speech.local.runsOn;
        return localGraphicsCardOptions(facts().localGpus, runsOn).size() > 1
            || (runsOn.backend == QStringLiteral("auto") && !runsOn.deviceId.isEmpty());
    };

    SettingsRow folder = actionRow(QStringLiteral("localModelFolder"),
                                   QStringLiteral("Model folder"),
                                   QStringLiteral("Where downloaded models are kept."),
                                   QStringLiteral("Open model folder"));
    folder.helpValue = [facts](const AppSettings &) { return facts().modelFolder; };

    return {
        QStringLiteral("localModels"),
        {
            {QStringLiteral("Speech models"),
             QStringLiteral("Models run on this computer, with no account and no network once "
                            "downloaded."),
             {std::move(browser)}},
            {QStringLiteral("Performance and storage"),
             QString(),
             {std::move(idleUnload), std::move(acceleration), std::move(graphicsCard), std::move(folder)}},
        },
    };
}

// The Writing Profile overrides a recognition rule replaced are folded in on
// read and dropped on write, so opening the page once retires them.
QList<QVariantMap> recognitionRecords(const AppSettings &settings)
{
    QList<QVariantMap> records;
    // A profile deleted in the draft shows as none, which is what saving makes it.
    const auto append = [&records, &settings](const AppRecognitionRule &rule, const QString &source) {
        records.append({
            {kMatchColumn, rule.match},
            {kCategoryColumn, rule.category ? appCategoryName(*rule.category) : QString()},
            {kProfileColumn,
             offeredWritingProfile(rule.writingProfile.value_or(QString()),
                                   settings.refinement.writingProfiles, QString())},
            {kSourceColumn, source},
        });
    };
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        append(rule, QStringLiteral("Built-in"));
    }
    for (const AppRecognitionRule &rule : recognitionRulesWithMigratedProfileOverrides(
             settings.appRecognitionRules, settings.refinement.writingProfileOverrides)) {
        append(rule, QStringLiteral("Custom"));
    }
    return records;
}

QList<AppRecognitionRule> recognitionRules(const QList<QVariantMap> &records)
{
    QList<AppRecognitionRule> rules;
    for (const QVariantMap &record : records) {
        AppRecognitionRule rule;
        rule.match = record.value(kMatchColumn).toString().trimmed();
        const QString category = record.value(kCategoryColumn).toString();
        const QString profile = record.value(kProfileColumn).toString();
        if (!category.isEmpty()) {
            rule.category = appCategoryFromName(category);
        }
        if (!profile.isEmpty()) {
            rule.writingProfile = writingProfileFromName(profile);
        }
        if (!rule.match.isEmpty() && (rule.category || rule.writingProfile)) {
            rules.append(rule);
        }
    }
    return rules;
}

SettingsSection applicationRecognitionSection()
{
    CollectionDescriptor rules;
    rules.columns = {
        {kMatchColumn,
         QStringLiteral("App name or ID"),
         ColumnKind::Text,
         {},
         true,
         QStringLiteral("Matches the application ID, application name, process name, or accessible role.")},
        {kCategoryColumn, QStringLiteral("App type"), ColumnKind::Choice, fixedOptions(appCategoryOptions())},
        {kProfileColumn, QStringLiteral("Writing Profile"), ColumnKind::Choice, writingProfileOptions},
        {kSourceColumn, QStringLiteral("Source"), ColumnKind::ReadOnly},
    };
    rules.records = recognitionRecords;
    rules.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.appRecognitionRules = recognitionRules(records);
        settings.refinement.writingProfileOverrides.clear();
    };
    rules.blankRecord = {{kSourceColumn, QStringLiteral("Custom")}};
    rules.lockedRecordCount = [] { return int(builtInAppRecognitionRules().size()); };
    rules.addLabel = QStringLiteral("Add application");
    rules.addDialogTitle = QStringLiteral("New application match");
    rules.minimumHeight = 320;

    SettingsRow row = collectionRow(
        QStringLiteral("appRecognitionRules"),
        QString(),
        QStringLiteral("Your matches come first and set the app type for paste rules, the Writing Profile "
                       "used on the %1 page, or both.")
            .arg(paneTitle(QStringLiteral("writingProfiles"))),
        std::move(rules));
    gateOnTargetAccessibility(
        row, accessibilityGateHelp(QStringLiteral("identify target applications")));

    return {QStringLiteral("Application recognition"), QString(), {std::move(row)}};
}

QList<PasteRule> applicationPasteRules(const QList<QVariantMap> &records)
{
    QList<PasteRule> rules;
    for (const QVariantMap &record : records) {
        const QString applicationId = record.value(kApplicationColumn).toString().trimmed();
        if (applicationId.isEmpty()) {
            continue;
        }
        rules.append({PasteRuleScope::Application,
                      applicationId,
                      pasteMethodFromName(record.value(kMethodColumn).toString()),
                      record.value(kEnabledColumn).toBool()});
    }
    return rules;
}

SettingsRow applicationPasteRuleRow()
{
    CollectionDescriptor descriptor;
    descriptor.columns = {
        {kEnabledColumn, QStringLiteral("Enabled"), ColumnKind::Toggle},
        {kApplicationColumn,
         QStringLiteral("Application ID"),
         ColumnKind::Text,
         {},
         true,
         applicationIdHint()},
        {kMethodColumn,
         QStringLiteral("Paste method"),
         ColumnKind::Choice,
         fixedOptions(pasteMethodOptions(false))},
    };
    descriptor.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const PasteRule &rule : settings.output.pasteRules) {
            if (rule.scope == PasteRuleScope::Application) {
                records.append({{kEnabledColumn, rule.enabled},
                                {kApplicationColumn, rule.match},
                                {kMethodColumn, shownPasteMethod(rule.method)}});
            }
        }
        return records;
    };
    descriptor.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QList<PasteRule> kept;
        for (const PasteRule &rule : settings.output.pasteRules) {
            if (rule.scope != PasteRuleScope::Application) {
                kept.append(rule);
            }
        }
        settings.output.pasteRules = applicationPasteRules(records) + kept;
    };
    descriptor.blankRecord = {{kEnabledColumn, true},
                              {kApplicationColumn, QString()},
                              {kMethodColumn, pasteMethodName(PasteMethod::StandardPaste)}};
    descriptor.validate = [](const QList<QVariantMap> &records) {
        return validatePasteRules(applicationPasteRules(records));
    };
    descriptor.addLabel = QStringLiteral("Add rule");
    descriptor.addDialogTitle = QStringLiteral("New paste rule");
    descriptor.emptyTitle = QStringLiteral("No app-specific paste rules");
    descriptor.emptyHelp = QStringLiteral("Add one to paste differently into a single application.");
    descriptor.minimumHeight = 150;

    SettingsRow row = collectionRow(QStringLiteral("applicationPasteRules"),
                                    QStringLiteral("App-specific paste rules"),
                                    applicationPasteRuleHint(),
                                    std::move(descriptor));
    return row;
}

SettingsRow categoryPasteRuleRow(AppCategory category)
{
    const QString match = appCategoryName(category);
    return choiceRow(
        QStringLiteral("categoryPasteRule_") + match,
        pasteCategoryLabel(category),
        QString(),
        fixedOptions(pasteMethodOptions(true)),
        [match](const AppSettings &settings) {
            for (const PasteRule &rule : settings.output.pasteRules) {
                if (rule.scope == PasteRuleScope::Category && rule.match == match) {
                    return shownPasteMethod(rule.method);
                }
            }
            return inheritGlobalPasteRule();
        },
        [match](AppSettings &settings, const QString &value) {
            if (value == inheritGlobalPasteRule()) {
                removePasteRule(settings.output.pasteRules, PasteRuleScope::Category, match);
                return;
            }
            setPasteRule(settings.output.pasteRules,
                         PasteRuleScope::Category,
                         match,
                         pasteMethodFromName(value));
        });
}

// The Paste with choices this platform can deliver with: Automatic, or its
// keyboard paste alone. Whether to insert directly or only copy is the default
// paste's choice. Linux's virtual keyboard entry is among them; the Qt front
// end marks it unavailable until it is set up.
QList<RowOption> outputMethodOptions()
{
    QList<RowOption> options;
    for (const char *method : {OutputMethod::Automatic,
#ifdef Q_OS_MACOS
                               OutputMethod::MacPaste,
#elif defined(Q_OS_WIN)
                               OutputMethod::WinPaste,
#else
                               OutputMethod::Ydotool,
#endif
         }) {
        const QString id = QString::fromLatin1(method);
        options.append({id, OutputMethod::label(id)});
    }
    return options;
}

// What Automatic does, which differs per platform.
QString automaticOutputMethodHelp()
{
    const QString limits = QStringLiteral(" Default paste and the paste rules can limit it to inserting "
                                          "or copying.");
#ifdef Q_OS_MACOS
    return QStringLiteral("Automatic inserts text directly where it can, then pastes with Cmd+V.") + limits;
#elif defined(Q_OS_WIN)
    return QStringLiteral("Automatic inserts text directly where it can, then pastes with Ctrl+V.") + limits;
#else
    return QStringLiteral("Automatic inserts text directly where it can, then pastes with the "
                          "virtual keyboard once it is set up.")
        + limits;
#endif
}

SettingsPage outputPage(const SchemaContext &context)
{
    SettingsRow method = customRow(QStringLiteral("outputMethod"),
                                   QStringLiteral("Paste with"),
                                   automaticOutputMethodHelp());
    method.options = fixedOptions(outputMethodOptions());
    method.value = [](const AppSettings &settings) { return QVariant(settings.output.method); };
    method.apply = [](AppSettings &settings, const QVariant &value) {
        settings.output.method = value.toString();
    };

    QList<SettingsRow> deliveryRows{std::move(method)};
    // Paste with relies on it, so it sits right under that choice.
    if (context.virtualKeyboardSetup) {
        deliveryRows.append(customRow(QStringLiteral("virtualKeyboard"),
                                      QStringLiteral("Virtual keyboard"),
                                      QStringLiteral("Lets Speecher press the paste keys in other apps.")));
    }
    // The paste rule every app gets unless a rule below names its type or
    // the app itself.
    deliveryRows.append(choiceRow(
        QStringLiteral("globalPasteRule"),
        QStringLiteral("Default paste"),
        QStringLiteral("How Speecher pastes unless a paste rule says otherwise."),
        fixedOptions(pasteMethodOptions(false)),
        [](const AppSettings &settings) {
            for (const PasteRule &rule : settings.output.pasteRules) {
                if (rule.scope == PasteRuleScope::Global) {
                    return shownPasteMethod(rule.method);
                }
            }
            return pasteMethodName(PasteMethod::StandardPaste);
        },
        [](AppSettings &settings, const QString &value) {
            setPasteRule(settings.output.pasteRules,
                         PasteRuleScope::Global,
                         QString(),
                         pasteMethodFromName(value));
        }));
    SettingsRow format = choiceRow(
        QStringLiteral("outputFormat"),
        QStringLiteral("Paste as"),
        QStringLiteral("Formatted text keeps lists and emphasis in apps that accept it."),
        fixedOptions({
            {QStringLiteral("plain"), QStringLiteral("Plain text")},
            {QStringLiteral("html"), QStringLiteral("Formatted text (HTML)")},
        }),
        [](const AppSettings &settings) { return outputFormatName(settings.output.format); },
        [](AppSettings &settings, const QString &value) {
            settings.output.format = outputFormatFromString(value);
        });
    format.tooltip = QStringLiteral("A CLI shortcut can override this per dictation.");
    deliveryRows.append(std::move(format));
    deliveryRows.append(toggleRow(
        QStringLiteral("restoreClipboardAfterTyping"),
        QStringLiteral("Restore the clipboard after pasting"),
        restoreClipboardDescription(),
        [](const AppSettings &settings) { return settings.output.restoreClipboardAfterTyping; },
        [](AppSettings &settings, bool value) { settings.output.restoreClipboardAfterTyping = value; }));

    // Every category rule needs a known target application, so they stand or
    // fall together with desktop accessibility.
    QList<SettingsRow> categoryRows;
    for (AppCategory category : managedPasteCategories()) {
        SettingsRow row = categoryPasteRuleRow(category);
        row.groupId = QStringLiteral("targetPasteControls");
        gateOnTargetAccessibility(row, targetAccessibilityHint());
        categoryRows.append(std::move(row));
    }
    SettingsRow applicationRules = applicationPasteRuleRow();
    gateOnTargetAccessibility(applicationRules, targetAccessibilityHint());

    // App recognition rules decide which paste rule applies, so they live with
    // the paste rules instead of on a page of their own.
    return {
        QStringLiteral("output"),
        {
            {QStringLiteral("Delivery"), QString(), std::move(deliveryRows)},
            {QStringLiteral("Paste rules"),
             QStringLiteral("Overrides the default paste for apps of each type."),
             std::move(categoryRows)},
            {QStringLiteral("App-specific paste rules"), QString(), {std::move(applicationRules)}},
            applicationRecognitionSection(),
        },
    };
}

QString lastUsedLabel(qint64 lastUsedMs)
{
    return lastUsedMs > 0
        ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(lastUsedMs), QLocale::ShortFormat)
        : QStringLiteral("Never");
}

QStringList vocabularyTerms(const QList<VocabularyEntry> &entries)
{
    QStringList terms;
    terms.reserve(entries.size());
    for (const VocabularyEntry &entry : entries) {
        terms.append(entry.term);
    }
    return terms;
}

// Where a term came from, as the Source column says it. The stored id stays
// under kSourceIdKey, since the column only shows it.
// The terms marked to go to the speech service, in the order they would.
QStringList vocabularyKeyTerms(const QList<VocabularyEntry> &entries)
{
    QStringList terms;
    for (const VocabularyEntry &entry : entries) {
        if (entry.keyTerm) {
            terms.append(entry.term);
        }
    }
    return terms;
}

QString vocabularySourceLabel(const QString &source)
{
    if (source == QStringLiteral("csv")) {
        return QStringLiteral("Imported");
    }
    if (source == QStringLiteral("learned")) {
        return QStringLiteral("Learned");
    }
    return QStringLiteral("Added");
}

QList<QVariantMap> vocabularyRecords(const QList<VocabularyEntry> &entries)
{
    QList<QVariantMap> records;
    records.reserve(entries.size());
    for (const VocabularyEntry &entry : entries) {
        records.append({
            {kStarColumn, entry.starred},
            {kTermColumn, entry.term},
            {kSourceColumn, vocabularySourceLabel(entry.source)},
            {kSourceIdKey, entry.source.isEmpty() ? QStringLiteral("manual") : entry.source},
            {kUsesColumn, qMax(0, entry.frequency)},
            {kLastUsedColumn, lastUsedLabel(entry.lastUsedMs)},
            {kLastUsedMsKey, entry.lastUsedMs},
            {kContextColumn, entry.context},
            {kProfilesColumn, entry.profiles},
            {kKeyTermColumn, entry.keyTerm},
        });
    }
    return records;
}

QList<VocabularyEntry> vocabularyEntries(const QList<QVariantMap> &records)
{
    QList<VocabularyEntry> entries;
    entries.reserve(records.size());
    for (const QVariantMap &record : records) {
        const QString term = record.value(kTermColumn).toString();
        if (term.trimmed().isEmpty()) {
            continue;
        }
        entries.append({term,
                        record.value(kSourceIdKey).toString(),
                        record.value(kStarColumn).toBool(),
                        record.value(kUsesColumn).toInt(),
                        record.value(kLastUsedMsKey).toLongLong(),
                        record.value(kContextColumn).toString(),
                        record.value(kProfilesColumn).toStringList(),
                        record.value(kKeyTermColumn, true).toBool()});
    }
    return normalizeVocabularyEntries(entries);
}

SettingsPage vocabularyPage()
{
    CollectionDescriptor terms;
    terms.identityColumn = kTermColumn;
    CollectionColumn term{kTermColumn, QStringLiteral("Term"), ColumnKind::Text, {}, true};
    term.detailColumn = kContextColumn;
    CollectionColumn context{kContextColumn, QStringLiteral("Context"), ColumnKind::Text};
    context.multiline = true;
    context.dialogOnly = true;
    context.placeholder = QStringLiteral("What it means and when it applies, such as "
                                         "\"the container platform, when I talk about clusters or deploys\".");
    context.help = QStringLiteral("Refinement reads this to decide when the words you said mean this term.");
    CollectionColumn profiles{kProfilesColumn, QStringLiteral("Profiles"), ColumnKind::ChoiceSet,
                              [](const AppSettings &settings) {
                                  return writingProfileChoices(settings.refinement.writingProfiles);
                              }};
    profiles.everyLabel = QStringLiteral("All");
    profiles.everyChoice = QStringLiteral("Every Writing Profile");
    profiles.someChoice = QStringLiteral("Only these Writing Profiles:");
    profiles.help = QStringLiteral("Under any other profile, neither refinement nor the speech service gets this term.");
    CollectionColumn keyTerm{kKeyTermColumn, QStringLiteral("Key term"), ColumnKind::Toggle};
    keyTerm.dialogOnly = true;
    keyTerm.help = QStringLiteral("Sent to the speech service as a hint, so it hears the term. "
                                  "Refinement uses every term either way.");
    CollectionColumn priority{kStarColumn, QStringLiteral("Priority"), ColumnKind::Toggle};
    priority.dialogOnly = true;
    priority.enabledBy = kKeyTermColumn;
    priority.help = QStringLiteral("Puts the key term first in line for the speech service, so it stays "
                                   "in when the list is longer than the service takes.");
    // The table shows both as badges, so neither takes a column.
    terms.columns = {
        term,
        keyTerm,
        priority,
        context,
        profiles,
        {kSourceColumn, QStringLiteral("Source"), ColumnKind::ReadOnly},
        {kUsesColumn, QStringLiteral("Uses"), ColumnKind::ReadOnly},
        {kLastUsedColumn, QStringLiteral("Last used"), ColumnKind::ReadOnly},
    };
    terms.records = [](const AppSettings &settings) {
        return vocabularyRecords(normalizeVocabularyEntries(settings.vocabulary));
    };
    // Normalising here rather than on every cell edit is what lets a person
    // finish typing a term that momentarily duplicates another one.
    terms.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.vocabulary = vocabularyEntries(records);
    };
    terms.blankRecord = {{kStarColumn, false},
                         {kKeyTermColumn, true},
                         {kTermColumn, QString()},
                         {kSourceColumn, vocabularySourceLabel(QStringLiteral("manual"))},
                         {kSourceIdKey, QStringLiteral("manual")},
                         {kUsesColumn, 0},
                         {kLastUsedColumn, lastUsedLabel(0)},
                         {kLastUsedMsKey, qint64(0)},
                         {kContextColumn, QString()},
                         {kProfilesColumn, QStringList()}};
    terms.badges = [](const QList<QVariantMap> &records, const AppSettings &settings) {
        QStringList badges(records.size());
        // The same entries the settings would store, so the badges follow
        // the priority order the speech request is cut from: a key term
        // past what the service takes gets none.
        const QStringList hints = VocabularyLimit::speechKeyterms(
            vocabularyKeyTerms(vocabularyEntries(records)), settings.speech.providerId);
        for (int index = 0; index < records.size(); ++index) {
            if (hints.contains(records.at(index).value(kTermColumn).toString().simplified(),
                               Qt::CaseInsensitive)) {
                badges[index] = QStringLiteral("Key term");
            }
        }
        return badges;
    };
    // Priority only means anything for a key term.
    terms.detailBadges = [](const QList<QVariantMap> &records, const AppSettings &) {
        QStringList badges(records.size());
        for (int index = 0; index < records.size(); ++index) {
            const QVariantMap &record = records.at(index);
            if (record.value(kStarColumn).toBool() && record.value(kKeyTermColumn, true).toBool()) {
                badges[index] = QStringLiteral("Priority");
            }
        }
        return badges;
    };
    terms.addLabel = QStringLiteral("Add");
    terms.addDialogTitle = QStringLiteral("New term");
    terms.editLabel = QStringLiteral("Edit…");
    terms.emptyTitle = QStringLiteral("No vocabulary terms");
    terms.emptyHelp = QStringLiteral("Add names and words the speech service should spell your way.");
    terms.supportsImport = {
        QStringLiteral("Import CSV…"),
        QStringLiteral("CSV files (*.csv);;All files (*)"),
        QStringLiteral("Vocabulary not imported"),
        [](const QByteArray &csv, QString *error) {
            return vocabularyRecords(parseVocabularyCsv(csv, error));
        },
    };
    // These three are the whole page they sit on, so they get the editor a
    // page can hold rather than the one an embedded table needs.
    terms.minimumHeight = 320;

    SettingsRow limit;
    limit.id = QStringLiteral("vocabularyLimit");
    limit.label = QStringLiteral("Limit");
    limit.kind = RowKind::Info;
    limit.value = [](const AppSettings &settings) {
        const QList<VocabularyEntry> entries = normalizeVocabularyEntries(settings.vocabulary);
        return QVariant(VocabularyLimit::summary(
            vocabularyTerms(entries), vocabularyKeyTerms(entries), settings.speech.providerId));
    };

    const QString help = QStringLiteral("Refinement uses the terms for the dictation's Writing Profile. "
                                        "Key terms also go to the speech service.");
    // The view names it, so the table needs no label of its own.
    SettingsRow entries = collectionRow(QStringLiteral("vocabularyEntries"),
                                        QString(),
                                        help,
                                        std::move(terms));
    entries.helpValue = [](const AppSettings &settings) {
        const QString &provider = settings.speech.providerId;
        const QString speech = provider == QStringLiteral("claude")
            ? QStringLiteral("Key terms also go to Claude Voice.")
            : provider == QStringLiteral("endpoint")
            ? QStringLiteral("Key terms also go to the Custom Endpoint, as its prompt.")
            : QStringLiteral("This speech service takes no key terms.");
        return QStringLiteral("Refinement uses the terms for the dictation's Writing Profile. ") + speech;
    };

    return {
        QStringLiteral("vocabulary"),
        {{QStringLiteral("Terms"),
          QString(),
          {
              std::move(entries),
              std::move(limit),
          }}},
    };
}

QList<LearnedCorrection> learnedCorrections(const QList<QVariantMap> &records)
{
    QList<LearnedCorrection> corrections;
    corrections.reserve(records.size());
    for (const QVariantMap &record : records) {
        LearnedCorrection correction;
        correction.id = record.value(kCorrectionIdKey).toString();
        correction.original = record.value(kHeardColumn).toString().trimmed();
        correction.corrected = record.value(kCorrectedColumn).toString().trimmed();
        correction.applicationId = record.value(kCorrectedAppColumn).toString().trimmed();
        correction.createdAtMs = record.value(kCreatedAtKey).toLongLong();
        correction.confidence = record.value(kConfidenceKey).toDouble();
        correction.enabled = record.value(kEnabledColumn).toBool();
        correction.evidenceCount = record.value(kEvidenceCountKey).toInt();
        correction.lastObservedAtMs = record.value(kLastObservedKey).toLongLong();
        if (!correction.id.isEmpty() && !correction.original.isEmpty()
            && !correction.corrected.isEmpty()) {
            corrections.append(correction);
        }
    }
    return corrections;
}

SettingsPage correctionsPage()
{
    SettingsRow learn = toggleRow(
        QStringLiteral("correctionLearningControl"),
        QStringLiteral("Learn corrections"),
        QStringLiteral("After inserting text, briefly watch for your edits to it and learn "
                       "repeated corrections."),
        [](const AppSettings &settings) { return settings.correctionLearningEnabled; },
        [](AppSettings &settings, bool value) { settings.correctionLearningEnabled = value; });
    gateOnTargetAccessibility(
        learn, accessibilityGateHelp(QStringLiteral("learn corrections after insertion")));

    CollectionDescriptor corrections;
    corrections.identityColumn = kCorrectionIdKey;
    corrections.columns = {
        {kEnabledColumn, QStringLiteral("Enabled"), ColumnKind::Toggle},
        {kHeardColumn, QStringLiteral("Heard"), ColumnKind::Text, {}, true},
        {kCorrectedColumn, QStringLiteral("Corrected"), ColumnKind::Text, {}, true},
        {kCorrectedAppColumn,
         QStringLiteral("App"),
         ColumnKind::ReadOnly,
         {},
         false,
         QString(),
         [](const QVariantMap &record) {
             return QStringLiteral("Learned automatically · confidence %1%")
                 .arg(qRound(record.value(kConfidenceKey).toDouble() * 100.0));
         }},
    };
    corrections.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        records.reserve(settings.learnedCorrections.size());
        for (const LearnedCorrection &correction : settings.learnedCorrections) {
            records.append({
                {kEnabledColumn, correction.enabled},
                {kHeardColumn, correction.original},
                {kCorrectedColumn, correction.corrected},
                {kCorrectedAppColumn, correction.applicationId},
                {kCorrectionIdKey, correction.id},
                {kCreatedAtKey, correction.createdAtMs},
                {kConfidenceKey, correction.confidence},
                {kEvidenceCountKey, correction.evidenceCount},
                {kLastObservedKey, correction.lastObservedAtMs},
            });
        }
        return records;
    };
    corrections.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.learnedCorrections = learnedCorrections(records);
    };
    // Corrections arrive from watching an edit, so there is nothing to add here.
    corrections.actions = {
        {QStringLiteral("undoLatestLearn"), QStringLiteral("Undo last correction")},
    };
    corrections.emptyTitle = QStringLiteral("No learned corrections yet");
    corrections.emptyHelp = QStringLiteral("When you fix a dictated word the same way more than "
                                           "once, the correction appears here.");
    corrections.minimumHeight = 320;

    return {
        QStringLiteral("corrections"),
        {{QStringLiteral("Learned corrections"),
          QString(),
          {
              std::move(learn),
              collectionRow(QStringLiteral("learnedCorrections"),
                            QString(),
                            QStringLiteral("Turn a correction off to stop using it."),
                            std::move(corrections)),
          }}},
    };
}

QList<BindingRule> bindingRules(const QList<QVariantMap> &records)
{
    QList<BindingRule> rules;
    rules.reserve(records.size());
    for (const QVariantMap &record : records) {
        rules.append({record.value(kPhraseColumn).toString(),
                      record.value(kReplacementColumn).toString()});
    }
    return rules;
}

QList<QVariantMap> bindingRecords(const QList<BindingRule> &rules)
{
    QList<QVariantMap> records;
    records.reserve(rules.size());
    for (const BindingRule &rule : rules) {
        records.append({{kPhraseColumn, rule.phrase}, {kReplacementColumn, rule.replacement}});
    }
    return records;
}

SettingsPage bindingsPage()
{
    CollectionDescriptor replacements;
    replacements.columns = {
        {kPhraseColumn, QStringLiteral("Spoken phrase"), ColumnKind::Text},
        {kReplacementColumn,
         QStringLiteral("Exact replacement or snippet"),
         ColumnKind::Text,
         {},
         true},
    };
    replacements.columns.last().multiline = true;
    replacements.columns.last().placeholder = QStringLiteral("Sent on {date} at {time}");
    replacements.records = [](const AppSettings &settings) {
        return bindingRecords(settings.bindings);
    };
    replacements.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.bindings = BindingProcessor::validateRules(bindingRules(records)).rules;
    };
    replacements.validate = [](const QList<QVariantMap> &records) {
        return BindingProcessor::validateRules(bindingRules(records)).messages();
    };
    replacements.blankRecord = {{kPhraseColumn, QString()}, {kReplacementColumn, QString()}};
    replacements.addLabel = QStringLiteral("Add replacement");
    replacements.addDialogTitle = QStringLiteral("New replacement");
    replacements.emptyTitle = QStringLiteral("No replacements or snippets");
    replacements.emptyHelp = QStringLiteral("Add a spoken phrase and the exact text to put in its place.");
    replacements.supportsImport = {
        QStringLiteral("Import snippets JSON…"),
        QStringLiteral("JSON files (*.json);;All files (*)"),
        QStringLiteral("Snippets not imported"),
        [](const QByteArray &json, QString *error) {
            return bindingRecords(BindingProcessor::parseJsonImport(json, error));
        },
    };
    replacements.minimumHeight = 320;

    // The view names it, so the table needs no label of its own.
    SettingsRow rules = collectionRow(QStringLiteral("bindingRules"),
                                      QString(),
                                      QStringLiteral("Replace a spoken phrase with exact text, including "
                                                     "multi-line snippets. {date} and {time} in the text "
                                                     "become today's date and the current time."),
                                      std::move(replacements));
    rules.tooltip = QStringLiteral("Matching ignores case and treats punctuation as spaces.");

    return {
        QStringLiteral("bindings"),
        {{QStringLiteral("Replacements & snippets"), QString(), {std::move(rules)}}},
    };
}

// What one refinement account contributes: Model, Effort and Speed under the
// Refinement pane's Provider picker, and a sign-in card on Accounts. A third
// provider is another entry in providerAccounts() plus the two AppSettings
// fields it names, rather than a third hand-written card.
struct ProviderAccount {
    // The refinement provider id these model rows belong to.
    QString providerId;
    // The sign-in's heading, on the Accounts pane.
    QString sectionTitle;
    // A closing note under the card.
    QString note;
    QString modelRowId;
    QString modelTooltip;
    int modelWidthHint = 0;
    QList<RowOption> models;
    QString RefinementSettings::*model;
    // Said only while the chosen model's id contains this, which is how a model
    // that reads a transcript as instructions warns about it.
    QString cautionWhenModelContains;
    QString caution;
    QString effortRowId;
    QString effortTooltip;
    QList<RowOption> efforts;
    QString RefinementSettings::*effort;
    // Standard or Fast for Anthropic; Standard, Fast or Ultrafast for OpenAI.
    SettingsRow speed;
    // Where the credentials come from is a question for a keyring rather than a
    // value in AppSettings, so every front end answers it its own way.
    QList<SettingsRow> authRows;
};

// The account picker for a provider is only worth showing while its credentials
// come from CLI Proxy API.
const QString kCliProxyAuthMode = QStringLiteral("cliproxy");

QList<RowOption> namedModels(const QStringList &ids)
{
    QList<RowOption> models;
    models.reserve(ids.size());
    for (const QString &id : ids) {
        models.append({id, id});
    }
    return models;
}

QList<ProviderAccount> providerAccounts()
{
    ProviderAccount openAi;
    openAi.providerId = QStringLiteral("openai");
    openAi.sectionTitle = QStringLiteral("OpenAI");
    // Footnote of the card whose Sign-in row it explains.
    openAi.note = QStringLiteral(
        "Automatic uses the first OpenAI sign-in it finds: the Codex app, then an API key from "
        "the environment or saved in Speecher.");
    openAi.modelRowId = QStringLiteral("openAiModel");
    openAi.modelTooltip = QStringLiteral("Defaults to gpt-6-luna with no reasoning effort. "
                                         "Select another model or type another model ID.");
    openAi.modelWidthHint = 16;
    openAi.models = namedModels({
        QStringLiteral("gpt-6-luna"),
        QStringLiteral("gpt-6.1-sol"),
        QStringLiteral("gpt-6-astra"),
        QStringLiteral("gpt-5.6-luna"),
        QStringLiteral("gpt-5.6-terra"),
        QStringLiteral("gpt-5.5"),
        QStringLiteral("gpt-5.4-nano"),
        QStringLiteral("gpt-5.4-mini"),
        QStringLiteral("gpt-5.4"),
    });
    openAi.model = &RefinementSettings::openAiModel;
    openAi.effortRowId = QStringLiteral("openAiEffort");
    // OpenAiRefiner sends the chosen effort verbatim, apart from None on
    // GPT-6.1 Sol, so an unsupported value comes back as a request error.
    openAi.effortTooltip = QStringLiteral("Supported values vary by model. One this model does not "
                                          "support may be rejected.");
    openAi.efforts = {
        {QStringLiteral("none"), QStringLiteral("None")},
        {QStringLiteral("low"), QStringLiteral("Low")},
        {QStringLiteral("medium"), QStringLiteral("Medium")},
        {QStringLiteral("high"), QStringLiteral("High")},
        {QStringLiteral("xhigh"), QStringLiteral("Extra high")},
    };
    openAi.effort = &RefinementSettings::openAiEffort;
    openAi.speed = choiceRow(
        QStringLiteral("openAiSpeed"),
        QStringLiteral("Speed"),
        openAiSpeedHelp(),
        [](const AppSettings &settings) { return openAiSpeedOptions(settings.refinement.openAiModel); },
        [](const AppSettings &settings) { return settings.refinement.openAiSpeed; },
        [](AppSettings &settings, const QString &value) { settings.refinement.openAiSpeed = value; });
    openAi.speed.tooltip = fastModeTooltip(QStringLiteral("openai"));
    SettingsRow openAiStatus = customRow(QStringLiteral("openAiAuth"), QStringLiteral("Status"), QString());
    // In key mode the row holds the key itself.
    openAiStatus.labelValue = [](const AppSettings &settings) {
        return settings.refinement.openAiAuthMode == QStringLiteral("settings") ? QStringLiteral("API key")
                                                                                : QStringLiteral("Status");
    };
    // Reading the app settings key means asking the keyring.
    openAiStatus.expensive = true;
    openAi.authRows = {
        customRow(QStringLiteral("openAiAuthMode"),
                  QStringLiteral("Sign-in"),
                  openAiSignInHelp()),
        customRow(QStringLiteral("openAiCliproxyAccount"),
                  QStringLiteral("Account"),
                  QStringLiteral("The CLI Proxy API account to use.")),
        std::move(openAiStatus),
    };
    openAi.authRows[0].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiAuthMode);
    };
    openAi.authRows[0].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiAuthMode = value.toString();
    };
    openAi.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiCliproxyAccount);
    };
    openAi.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiCliproxyAccount = value.toString();
    };
    openAi.authRows[1].visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.openAiAuthMode == kCliProxyAuthMode;
    };

    ProviderAccount anthropic;
    anthropic.providerId = QStringLiteral("anthropic");
    anthropic.sectionTitle = QStringLiteral("Anthropic");
    anthropic.note = QStringLiteral(
        "Claude Code sign-in uses whichever account Claude Code is signed in to, in its desktop app "
        "or with /login in the claude CLI.");
    anthropic.modelRowId = QStringLiteral("anthropicModel");
    anthropic.modelTooltip =
        QStringLiteral("Defaults to Claude Opus 5.5. Select a model or type another model ID.");
    anthropic.modelWidthHint = 24;
    anthropic.models = {
        {QStringLiteral("claude-opus-5-5"), QStringLiteral("Claude Opus 5.5")},
        {QStringLiteral("claude-opus-5"), QStringLiteral("Claude Opus 5")},
        {QStringLiteral("claude-sonnet-5-5"), QStringLiteral("Claude Sonnet 5.5")},
        {QStringLiteral("claude-haiku-4-5"), QStringLiteral("Claude Haiku 4.5")},
    };
    anthropic.model = &RefinementSettings::anthropicModel;
    anthropic.cautionWhenModelContains = QStringLiteral("haiku");
    anthropic.caution = QStringLiteral("Haiku may treat transcript as instructions.");
    anthropic.effortRowId = QStringLiteral("anthropicEffort");
    // AnthropicApiRefiner::apiEffortForModel rewrites an effort the model does
    // not support before the request goes out.
    anthropic.effortTooltip = QStringLiteral("Supported values vary by model. One this model does not "
                                             "support is replaced with the nearest one it does.");
    anthropic.efforts = {
        {QStringLiteral("low"), QStringLiteral("Low")},
        {QStringLiteral("medium"), QStringLiteral("Medium")},
        {QStringLiteral("high"), QStringLiteral("High")},
        {QStringLiteral("xhigh"), QStringLiteral("Extra high")},
        {QStringLiteral("max"), QStringLiteral("Max")},
    };
    anthropic.effort = &RefinementSettings::anthropicEffort;
    // Stored as a flag, offered as the same Speed choice OpenAI has.
    anthropic.speed = choiceRow(
        QStringLiteral("anthropicFastMode"),
        QStringLiteral("Speed"),
        fastModeHelp(QStringLiteral("anthropic")),
        fixedOptions({
            {QStringLiteral("standard"), QStringLiteral("Standard")},
            {QStringLiteral("fast"), QStringLiteral("Fast")},
        }),
        [](const AppSettings &settings) {
            return settings.refinement.anthropicFastMode ? QStringLiteral("fast") : QStringLiteral("standard");
        },
        [](AppSettings &settings, const QString &value) {
            settings.refinement.anthropicFastMode = value == QStringLiteral("fast");
        });
    anthropic.speed.tooltip = fastModeTooltip(QStringLiteral("anthropic"));
    SettingsRow anthropicStatus = customRow(QStringLiteral("anthropicAuth"), QStringLiteral("Status"), QString());
    // Reading Claude Code's login means asking the keyring. A CLI Proxy API
    // account has no check to report yet.
    anthropicStatus.expensive = true;
    anthropicStatus.visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.anthropicAuthMode != kCliProxyAuthMode;
    };
    anthropic.authRows = {
        customRow(QStringLiteral("anthropicAuthMode"),
                  QStringLiteral("Sign-in"),
                  QStringLiteral("Used for Claude Voice dictation and Anthropic cleanup.")),
        customRow(QStringLiteral("anthropicCliproxyAccount"),
                  QStringLiteral("Account"),
                  QStringLiteral("The CLI Proxy API account to use.")),
        std::move(anthropicStatus),
    };
    anthropic.authRows[0].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicAuthMode);
    };
    anthropic.authRows[0].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicAuthMode = value.toString();
    };
    anthropic.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicCliproxyAccount);
    };
    anthropic.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicCliproxyAccount = value.toString();
    };
    anthropic.authRows[1].visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.anthropicAuthMode == kCliProxyAuthMode;
    };

    return {openAi, anthropic};
}

QList<SettingsRow> providerModelRows()
{
    QList<SettingsRow> rows;
    for (const ProviderAccount &account : providerAccounts()) {
        SettingsRow model;
        model.id = account.modelRowId;
        model.label = QStringLiteral("Model");
        model.kind = RowKind::Text;
        model.tooltip = account.modelTooltip;
        model.contentWidthHint = account.modelWidthHint;
        model.suggestions = fixedOptions(account.models);
        model.value = [field = account.model](const AppSettings &settings) {
            return QVariant(settings.refinement.*field);
        };
        model.apply = [field = account.model](AppSettings &settings, const QVariant &value) {
            settings.refinement.*field = value.toString();
        };

        QList<SettingsRow> accountRows{std::move(model)};
        if (!account.caution.isEmpty()) {
            SettingsRow caution = infoRow(account.modelRowId + QStringLiteral("Caution"),
                                          QStringLiteral("Caution"),
                                          QString(),
                                          account.caution);
            caution.visible = [field = account.model,
                               needle = account.cautionWhenModelContains,
                               provider = account.providerId](const AppSettings &settings,
                                                              const Capabilities &) {
                return settings.refinement.providerId == provider
                    && (settings.refinement.*field).toCaseFolded().contains(needle);
            };
            accountRows.append(std::move(caution));
        }
        accountRows.append(choiceRow(
            account.effortRowId,
            QStringLiteral("Thinking"),
            QStringLiteral("More thinking follows instructions more closely but takes longer."),
            fixedOptions(account.efforts),
            [field = account.effort](const AppSettings &settings) { return settings.refinement.*field; },
            [field = account.effort](AppSettings &settings, const QString &value) {
                settings.refinement.*field = value;
            }));
        accountRows.last().tooltip = account.effortTooltip;
        accountRows.append(account.speed);
        for (SettingsRow &row : accountRows) {
            if (!row.visible) {
                row.visible = whileRefinementProvider(account.providerId);
            }
        }
        rows.append(accountRows);
    }
    return rows;
}

// Only a provider actually routed through the CLI Proxy API server needs this
// card; a person using API keys or CLI tokens directly has nothing to set here.
bool cliproxyAccountsInUse(const AppSettings &settings, const Capabilities &)
{
    return settings.refinement.openAiAuthMode == kCliProxyAuthMode
        || settings.refinement.anthropicAuthMode == kCliProxyAuthMode;
}

// Refinement's CLI Proxy API server preset reads the server URL and key from
// this card, so they show for it too, without the account files it never uses.
bool cliproxyServerRowVisible(const AppSettings &settings, const Capabilities &capabilities)
{
    return cliproxyAccountsInUse(settings, capabilities)
        || (settings.refinement.providerId == QStringLiteral("endpoint")
            && settings.refinement.endpoint.preset == QStringLiteral("cliproxy"));
}

SettingsSection cliproxyServerSection()
{
    // Custom on the Qt frontend so editing it refreshes the account pickers
    // live; the other frontends fall back to their plain text field.
    SettingsRow oauthDir = customRow(
        QStringLiteral("cliproxyOauthDir"),
        QStringLiteral("Account directory"),
        QStringLiteral("Directory holding CLI Proxy API's saved account files."));
    oauthDir.placeholder = QStringLiteral("Leave empty to detect it automatically");
    oauthDir.sinceVersion = QStringLiteral("0.2.0");
    oauthDir.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyOauthDirConfigured);
    };
    oauthDir.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyOauthDirConfigured = value.toString().trimmed();
    };
    oauthDir.visible = cliproxyAccountsInUse;

    SettingsRow baseUrl = customRow(
        QStringLiteral("cliproxyBaseUrl"),
        QStringLiteral("Server URL"),
        QStringLiteral("Send text cleanup through this CLI Proxy API server."));
    baseUrl.placeholder = QStringLiteral("Leave empty to use the account files on this computer");
    baseUrl.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyBaseUrl);
    };
    baseUrl.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyBaseUrl = endpointServerBase(value.toString());
    };
    baseUrl.visible = cliproxyServerRowVisible;

    SettingsRow apiKey = customRow(
        QStringLiteral("cliproxyApiKey"),
        QStringLiteral("Server API key"),
        QStringLiteral("One of the keys the server accepts. Needed when a server URL is set. ")
            + keyStorageHelp());
    apiKey.placeholder = QStringLiteral("A key the server accepts");
    apiKey.secret = true;
    apiKey.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyApiKey);
    };
    apiKey.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyApiKey = value.toString().trimmed();
    };
    apiKey.visible = cliproxyServerRowVisible;

    return {QStringLiteral("CLI Proxy API"),
            QString(),
            {std::move(oauthDir), std::move(baseUrl), std::move(apiKey)}};
}

SettingsPage providersPage()
{
    QList<SettingsSection> sections;
    for (const ProviderAccount &account : providerAccounts()) {
        sections.append({account.sectionTitle, account.note, account.authRows});
    }
    sections.append(cliproxyServerSection());
    return {
        QStringLiteral("providers"),
        sections,
    };
}

} // namespace

QString openAiSignInHelp()
{
    return QStringLiteral("API keys only cover cleanup; dictation needs a ChatGPT or CLI Proxy API sign-in.");
}

QString fastModeHelp(const QString &refinementProviderId)
{
    return refinementProviderId == QStringLiteral("openai")
        ? QStringLiteral("Faster answers for slightly more usage.")
        : QStringLiteral("Fast answers sooner and spends usage credits.");
}

QString openAiSpeedHelp()
{
    return QStringLiteral("Fast answers sooner for slightly more usage.");
}

QList<RowOption> openAiSpeedOptions(const QString &model)
{
    const bool ultrafast = openAiModelSupportsUltrafast(model);
    return {
        {QStringLiteral("standard"), QStringLiteral("Standard")},
        {QStringLiteral("fast"), QStringLiteral("Fast")},
        {QStringLiteral("ultrafast"), QStringLiteral("Ultrafast"),
         ultrafast ? QString()
                   : QStringLiteral("Needs GPT-6 Astra. GPT-6.1 Sol support is coming later."),
         ultrafast},
    };
}

QString fastModeTooltip(const QString &refinementProviderId)
{
    return refinementProviderId == QStringLiteral("openai")
        ? QStringLiteral("Ultrafast is much faster but uses a lot more usage, and needs a plan with "
                         "Ultrafast access. A fast or ultrafast request that fails is sent again at "
                         "Standard.")
        : QStringLiteral("Only Opus models support Fast; other models refine at Standard.");
}

QString keyStorageHelp()
{
    return QStringLiteral("Stored in the system keychain when there is one.");
}

// macOS calls it the Accessibility permission; Linux desktops expose AT-SPI,
// which the rest of the UI calls desktop accessibility.
QString accessibilityGateHelp(const QString &purpose)
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Grant Accessibility permission to %1.").arg(purpose);
#elif defined(Q_OS_WIN)
    return QStringLiteral("UI Automation must be available to %1.").arg(purpose);
#else
    return QStringLiteral("Turn on desktop accessibility to %1.").arg(purpose);
#endif
}

QString accessibilityGrantActionLabel()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Open Accessibility settings");
#else
    return QStringLiteral("Enable desktop accessibility");
#endif
}

QString lookingForRunnersStatus()
{
    return QStringLiteral("Looking for Ollama, LM Studio and llama-server…");
}

QString checkingCredentialsStatus()
{
    return QStringLiteral("Checking credentials…");
}

QString restoreClipboardDescription()
{
    return QStringLiteral("If Speecher cannot confirm the paste, your dictation stays on the clipboard.");
}

QString globalShortcutPrompt()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Press a key combination, or a single key such as Right Option or F13.");
#else
    return QStringLiteral("Press a key combination, or a single key such as Right Alt or F13.");
#endif
}

QString globalShortcutChangeCaption()
{
    return QStringLiteral("Change…");
}

QString globalShortcutResetCaption(const QString &defaultShortcut)
{
    return QStringLiteral("Reset to %1").arg(defaultShortcut);
}

QString globalShortcutUnsetText()
{
    return QStringLiteral("Not set");
}

QString globalShortcutSetCaption()
{
    return QStringLiteral("Set shortcut");
}

QString globalShortcutSingleKeyCaption()
{
    return QStringLiteral("Set single key");
}

QString globalShortcutChooseCaption()
{
    return QStringLiteral("Choose shortcut");
}

QString globalShortcutClearCaption()
{
    return QStringLiteral("Clear");
}

QString globalShortcutBindFailedText()
{
    return QStringLiteral("That shortcut could not be bound.");
}

QString noSettingsMatchText()
{
    return QStringLiteral("No settings match");
}

const SettingsPage &SettingsSchema::page(const QString &id) const
{
    for (const SettingsPage &candidate : pages) {
        if (candidate.id == id) {
            return candidate;
        }
    }
    qFatal("no settings page with id %s", qPrintable(id));
}

bool SettingsSchema::hasPage(const QString &id) const
{
    return std::any_of(pages.cbegin(), pages.cend(), [&id](const SettingsPage &page) { return page.id == id; });
}

QList<RowOption> localAccelerationOptions(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen)
{
    QList<RowOption> options;
    for (const LocalBackend &backend : localBackends) {
        const QString kind = QString::fromLatin1(backend.kind);
        const bool gpuBackend = kind != QStringLiteral("auto") && kind != QStringLiteral("cpu");
        const bool present = std::any_of(gpus.cbegin(), gpus.cend(),
                                         [&kind](const LocalGpu &gpu) { return gpu.backend == kind; });
        if (!gpuBackend || present) {
            options.append({kind, QString::fromLatin1(backend.name)});
        }
    }
    if (std::none_of(options.cbegin(), options.cend(),
                     [&chosen](const RowOption &option) { return option.id == chosen.backend; })) {
        options.append({chosen.backend,
                        QStringLiteral("%1 (not available)").arg(localBackendName(chosen.backend)),
                        // ui-lint: allow avoid-term (an acceleration backend, not a Local Runner)
                        QStringLiteral("This computer has no graphics card this backend reaches."), false});
    }
    return options;
}

QList<RowOption> localGraphicsCardOptions(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen)
{
    if (chosen.backend == QStringLiteral("cpu")) {
        return {};
    }
    // Automatic lists no cards of its own, except one saved before
    // acceleration could be chosen: then the card decides the backend.
    const bool automatic = chosen.backend == QStringLiteral("auto");
    if (automatic && chosen.deviceId.isEmpty()) {
        return {};
    }
    QList<RowOption> options;
    for (const LocalGpu &gpu : gpus) {
        const bool matches = automatic ? gpu.deviceId == chosen.deviceId : gpu.backend == chosen.backend;
        const bool listed = std::any_of(options.cbegin(), options.cend(),
                                        [&gpu](const RowOption &option) { return option.id == gpu.deviceId; });
        if (matches && !listed) {
            options.append({gpu.deviceId, gpu.description});
        }
    }
    if (!chosen.deviceId.isEmpty()
        && std::none_of(options.cbegin(), options.cend(),
                        [&chosen](const RowOption &option) { return option.id == chosen.deviceId; })) {
        options.append({chosen.deviceId, QStringLiteral("Missing graphics card"),
                        QStringLiteral("This saved choice is not available on this computer."), false});
    }
    return options;
}

QString audioDeviceDefaultLabel()
{
    return QStringLiteral("System default");
}

QString microphoneTestCaption(MicrophoneTestState state)
{
    switch (state) {
    case MicrophoneTestState::Stopped:
        return QStringLiteral("Start test");
    case MicrophoneTestState::Starting:
        return QStringLiteral("Starting\u2026");
    case MicrophoneTestState::Running:
        return QStringLiteral("Stop test");
    }
    return QStringLiteral("Start test");
}

QList<RowOption> audioDeviceOptions(const QList<RowOption> &devices, const QString &selectedDeviceId)
{
    const RowOption missing{selectedDeviceId,
                            QStringLiteral("Missing microphone"),
                            QStringLiteral("This saved microphone is not currently available."),
                            false};
    if (devices.isEmpty()) {
        QList<RowOption> options{{QString(),
                                  QStringLiteral("No microphones found"),
                                  QStringLiteral("Connect or enable an input device, then try again."),
                                  false}};
        if (!selectedDeviceId.isEmpty()) {
            options.append(missing);
        }
        return options;
    }

    QList<RowOption> options{{QString(), audioDeviceDefaultLabel()}};
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const RowOption &device : devices) {
        options.append(device);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }
    if (!selectedFound) {
        options.append(missing);
    }
    return options;
}

QString nightlyChangesMarkdown(const QString &history, const QString &lastVersion,
                               const QString &currentVersion)
{
    if (!currentVersion.contains(QStringLiteral("-nightly"))) {
        return {};
    }
    const QString lastSha = versionCommit(lastVersion);
    const QString currentSha = versionCommit(currentVersion);
    if (lastSha.isEmpty() || currentSha.isEmpty()) {
        return {};
    }
    const QString compare = QStringLiteral("[Compare commits](%1/compare/%2...%3)")
                                .arg(kRepositoryUrl, lastSha, currentSha);

    QStringList bullets;
    bool lastShaFound = false;
    const QStringList records = history.split(QChar(0x1e), Qt::SkipEmptyParts);
    for (const QString &record : records) {
        const QStringList fields = record.split(QChar(0x1f));
        if (fields.size() < 2) {
            continue;
        }
        const QString sha = fields.at(0).trimmed();
        // Short SHAs grow with the repository, so either recording may be the
        // longer one.
        if (sha.startsWith(lastSha, Qt::CaseInsensitive)
            || lastSha.startsWith(sha, Qt::CaseInsensitive)) {
            lastShaFound = true;
            break;
        }
        bullets.append(commitBullet(sha,
                                    fields.at(1).trimmed(),
                                    fields.value(2).section(QLatin1Char('\n'), 0, 0).trimmed()));
    }
    // A previous nightly older than the embedded history window can only be
    // compared on GitHub.
    if (!lastShaFound || bullets.isEmpty()) {
        return compare;
    }
    return bullets.join(QLatin1Char('\n')) + QStringLiteral("\n\n") + compare;
}

int compareBaseVersions(const QString &left, const QString &right)
{
    static const QRegularExpression leadingDigits(QStringLiteral("^\\d+"));
    const auto component = [](const QStringList &parts, int index) {
        if (index >= parts.size()) {
            return 0;
        }
        return leadingDigits.match(parts.at(index)).captured().toInt();
    };
    const QStringList leftParts = left.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    const QStringList rightParts = right.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    for (int index = 0; index < qMax(leftParts.size(), rightParts.size()); ++index) {
        const int leftPart = component(leftParts, index);
        const int rightPart = component(rightParts, index);
        if (leftPart != rightPart) {
            return leftPart < rightPart ? -1 : 1;
        }
    }
    return 0;
}

namespace {

// A pane group as written below: the schema section it shows, and the view id
// an Alternatives pane addresses it by.
struct GroupSpec {
    const char *page;
    const char *section;
    const char *view = "";
};

struct PaneSpec {
    const char *id;
    const char *title;
    const char *iconId;
    PaneLayout layout;
    QList<GroupSpec> groups;
    QString intro;
};

// The settings window's panes on every front end. The pages above supply rows
// and values; which pane shows a section is decided here, and a pane group's
// heading is its section's title.
const QList<PaneSpec> &paneSpecs()
{
    static const QList<PaneSpec> specs{
        {"home", "Home", "home", PaneLayout::Home, {}},
        {"general", "General", "settings", PaneLayout::Sections,
         {{"general", "App"},
          {"general", "Insights"},
          {"general", "Updates"},
          {"general", "Uninstall"}}},
        // ui-lint: allow title-case: names the What's New Page.
        {"whatsNew", "What's New", "whatsNew", PaneLayout::Sections,
         {{"whatsNew", ""}, {"whatsNew", "Try the new settings"}}},
        {"dictation", "Dictation", "microphone", PaneLayout::Sections,
         {{"general", "Shortcut"},
          {"audio", "Transcription"},
          {"audio", "Microphone"},
          {"general", "While dictating"},
          {"audio", "Recording"}}},
        {"refinement", "Refinement", "refinement", PaneLayout::Sections,
         {{"refinement", "Provider"},
          {"refinement", "What refinement can see"}},
         refinementIntro() + QStringLiteral(" Choose None to paste your words as spoken.")},
        // ui-lint: allow title-case: the plural of the Writing Profile glossary term.
        {"writingProfiles", "Writing Profiles", "writingProfiles", PaneLayout::Sections,
         {{"writingProfiles", "Profiles"},
          {"writingProfiles", "Advanced"}},
         QStringLiteral("Speecher picks a Writing Profile from the app you're dictating into, then "
                        "cleans up your words to match it.")},
        {"localModels", "Local models", "localModels", PaneLayout::Sections,
         {{"localModels", "Speech models"},
          {"localModels", "Performance and storage"}}},
        {"transcribe", "Transcribe", "transcribe", PaneLayout::Transcribe, {}},
        {"output", "Output", "output", PaneLayout::Sections,
         {{"output", "Delivery"},
          {"output", "Paste rules"},
          {"output", "App-specific paste rules"},
          {"output", "Application recognition"}}},
        {"vocabulary", "Vocabulary", "vocabulary", PaneLayout::Alternatives,
         {{"vocabulary", "Terms", "terms"},
          {"corrections", "Learned corrections", "corrections"},
          {"bindings", "Replacements & snippets", "replacements"}}},
        {"accounts", "Accounts", "accounts", PaneLayout::Sections,
         {{"providers", "OpenAI"},
          {"providers", "Anthropic"},
          {"providers", "CLI Proxy API"}}},
    };
    return specs;
}

const QString kHomePane = QStringLiteral("home");

// A group whose section this build lacks or leaves empty (Uninstall outside
// Linux) is left out.
QList<SettingsPane> settingsPanes(const QList<SettingsPage> &pages)
{
    QList<SettingsPane> panes;
    for (const PaneSpec &spec : paneSpecs()) {
        SettingsPane pane{QLatin1String(spec.id), QLatin1String(spec.title),
                          QLatin1String(spec.iconId), spec.layout, {}, spec.intro};
        for (const GroupSpec &group : spec.groups) {
            for (const SettingsPage &page : pages) {
                if (page.id != QLatin1String(group.page)) {
                    continue;
                }
                for (const SettingsSection &section : page.sections) {
                    if (section.title != QLatin1String(group.section) || section.rows.isEmpty()) {
                        continue;
                    }
                    QStringList rows;
                    for (const SettingsRow &row : section.rows) {
                        rows.append(row.id);
                    }
                    pane.groups.append({QLatin1String(group.view), section.title, section.help, rows});
                }
            }
        }
        panes.append(std::move(pane));
    }
    return panes;
}

QList<SidebarGroup> settingsSidebarGroups()
{
    return {
        {QString(),
         {QStringLiteral("home"), QStringLiteral("transcribe"), QStringLiteral("general"),
          QStringLiteral("accounts")}},
        {QStringLiteral("Speech"), {QStringLiteral("dictation"), QStringLiteral("localModels")}},
        {QStringLiteral("Text"),
         {QStringLiteral("refinement"), QStringLiteral("writingProfiles"), QStringLiteral("vocabulary"),
          QStringLiteral("output")}},
    };
}

} // namespace

QString paneTitle(const QString &paneId)
{
    for (const PaneSpec &spec : paneSpecs()) {
        if (paneId == QLatin1String(spec.id)) {
            return QLatin1String(spec.title);
        }
    }
    qFatal("no settings pane with id %s", qPrintable(paneId));
}

const SettingsPane *SettingsSchema::pane(const QString &id) const
{
    for (const SettingsPane &candidate : panes) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

const SettingsRow *SettingsSchema::row(const QString &id) const
{
    for (const SettingsPage &candidate : pages) {
        for (const SettingsSection &section : candidate.sections) {
            for (const SettingsRow &row : section.rows) {
                if (row.id == id) {
                    return &row;
                }
            }
        }
    }
    return nullptr;
}

SettingsSection SettingsSchema::section(const SettingsPaneGroup &group) const
{
    SettingsSection section{group.title, group.help, {}};
    for (const QString &id : group.rows) {
        if (const SettingsRow *found = row(id)) {
            section.rows.append(*found);
        }
    }
    return section;
}

PageId resolvePage(const SettingsSchema &schema, const QString &request)
{
    // Pages that were once panes of their own, so old links and grabs land
    // where their settings live now.
    static const QHash<QString, QString> merged{
        {QStringLiteral("shortcut"), QStringLiteral("dictation")},
        {QStringLiteral("apps"), QStringLiteral("output")},
        {QStringLiteral("apps:recognition"), QStringLiteral("output")},
        {QStringLiteral("apps:pasterules"), QStringLiteral("output")},
    };
    if (const auto alias = merged.constFind(request.toLower()); alias != merged.cend()) {
        return resolvePage(schema, *alias);
    }
    const QString paneId = request.section(QLatin1Char(':'), 0, 0);
    const QString viewId = request.section(QLatin1Char(':'), 1);
    const SettingsPane *pane = nullptr;
    for (const SettingsPane &candidate : schema.panes) {
        if (candidate.id.compare(paneId, Qt::CaseInsensitive) == 0) {
            pane = &candidate;
        }
    }
    const bool alternatives = pane && pane->layout == PaneLayout::Alternatives && !pane->groups.isEmpty();
    if (pane && viewId.isEmpty()) {
        return {pane->id, alternatives ? pane->groups.first().view : QString()};
    }
    if (alternatives) {
        for (const SettingsPaneGroup &group : pane->groups) {
            if (group.view.compare(viewId, Qt::CaseInsensitive) == 0) {
                return {pane->id, group.view};
            }
        }
    }
    qWarning().noquote() << "no settings page" << request << "- showing Home";
    return {kHomePane, {}};
}

QList<SearchMatch> searchSettings(const SettingsSchema &schema, const QString &query, const AppSettings &settings,
                                  const Capabilities &capabilities)
{
    const auto hit = [needle = query.trimmed()](const QString &text) {
        return text.contains(needle, Qt::CaseInsensitive);
    };
    QList<SearchMatch> found;
    for (const SidebarGroup &group : schema.sidebarGroups) {
        for (const QString &id : group.panes) {
            const SettingsPane *pane = schema.pane(id);
            bool matches = hit(pane->title);
            QStringList rows;
            for (const SettingsPaneGroup &group : pane->groups) {
                matches = matches || hit(group.title) || hit(group.help);
                for (const QString &rowId : group.rows) {
                    const SettingsRow *row = schema.row(rowId);
                    // A row the pane is not showing is not something to find there.
                    if (row->visible && !row->visible(settings, capabilities)) {
                        continue;
                    }
                    if (hit(row->label) || hit(row->help) || hit(row->dialog.title)) {
                        rows.append(rowId);
                    }
                }
            }
            if (matches || !rows.isEmpty()) {
                found.append({id, rows});
            }
        }
    }
    return found;
}

QStringList searchPanes(const SettingsSchema &schema, const QString &query, const AppSettings &settings,
                        const Capabilities &capabilities)
{
    QStringList panes;
    for (const SearchMatch &match : searchSettings(schema, query, settings, capabilities)) {
        panes.append(match.pane);
    }
    return panes;
}

SettingsSchema buildSettingsSchema(const SchemaContext &context)
{
    QList<SettingsPage> pages{generalPage(context),
                              audioPage(context),
                              outputPage(context),
                              refinementPage(context),
                              writingProfilesPage(context),
                              vocabularyPage(),
                              correctionsPage(),
                              bindingsPage(),
                              providersPage()};
    QList<SidebarGroup> groups = settingsSidebarGroups();
    // Local models exists where this build runs speech models, which is when
    // the registry offers the local speech provider.
    const QString localModels = QStringLiteral("localModels");
    const bool localSpeech =
        std::any_of(context.speechProviders.cbegin(), context.speechProviders.cend(),
                    [](const RowOption &provider) { return provider.id == QStringLiteral("local"); });
    if (localSpeech) {
        pages.insert(4, localModelsPage(context));
    } else {
        for (SidebarGroup &group : groups) {
            group.panes.removeAll(localModels);
        }
    }
    pages.append(whatsNewPage(pages, context));
    QList<SettingsPane> panes = settingsPanes(pages);
    if (!localSpeech) {
        panes.removeIf([&localModels](const SettingsPane &pane) { return pane.id == localModels; });
    }
    return {std::move(pages), std::move(panes), std::move(groups)};
}

QString paneTitleForRow(const QString &rowId)
{
    // Which pane a row is on does not depend on the context beyond what it
    // enables, so one built with everything enabled answers for every build.
    static const SettingsSchema schema = [] {
        SchemaContext context;
        context.speechProviders = {{QStringLiteral("local"), QString()}};
        context.virtualKeyboardSetup = true;
        return buildSettingsSchema(context);
    }();
    for (const SettingsPane &pane : schema.panes) {
        for (const SettingsPaneGroup &group : pane.groups) {
            if (group.rows.contains(rowId)) {
                return pane.title;
            }
        }
    }
    qFatal("settings row %s is on no pane", qPrintable(rowId));
}

QString refinementIntro()
{
    return QStringLiteral("Refinement turns what you said into clean text before it's pasted: punctuation, "
                          "filler words and the cleanup your Writing Profile asks for.");
}

QList<RowOption> cleanupStrengths(const QList<CustomCleanupLevel> &custom)
{
    QList<RowOption> options{
        {QStringLiteral("none"), QStringLiteral("None"), QStringLiteral("Pastes your words as spoken, unless the profile translates.")},
        {QStringLiteral("light_cleanup"), QStringLiteral("Light"),
         QStringLiteral("Fixes punctuation, capitals and clear mistakes, and keeps your wording.")},
        {QStringLiteral("balanced"), QStringLiteral("Medium"),
         QStringLiteral("Also removes filler words and false starts, and adds paragraphs.")},
        {QStringLiteral("strong_polish"), QStringLiteral("High"),
         QStringLiteral("Also rewrites for clarity, flow and organization, keeping the facts.")},
    };
    for (const CustomCleanupLevel &level : custom) {
        const auto base = std::find_if(options.cbegin(), options.cend(),
                                       [&level](const RowOption &option) { return option.id == level.base; });
        options.append({level.id, level.name,
                        base == options.cend() ? QStringLiteral("Only your own instructions.")
                                               : QStringLiteral("%1, plus your own instructions.").arg(base->label)});
    }
    return options;
}

QList<RowOption> writingTones(const QList<CustomTone> &custom)
{
    QList<RowOption> options{
        {QStringLiteral("none"), QStringLiteral("No tone"), QStringLiteral("Keeps the tone you spoke in.")},
        {QStringLiteral("formal"), QStringLiteral("Formal"), QStringLiteral("Professional and polished.")},
        {QStringLiteral("casual"), QStringLiteral("Casual"), QStringLiteral("Relaxed and friendly.")},
        {QStringLiteral("very_casual"), QStringLiteral("Very casual"),
         QStringLiteral("Loose and conversational, like a text to a friend.")},
        {QStringLiteral("excited"), QStringLiteral("Excited"), QStringLiteral("Upbeat and enthusiastic.")},
        {QStringLiteral("gen_z"), QStringLiteral("Gen Z"), QStringLiteral("Gen Z slang and phrasing.")},
    };
    for (const CustomTone &tone : custom) {
        options.append({tone.id, tone.name, tone.instruction.simplified()});
    }
    return options;
}

QString offeredTone(const QString &id, const QList<CustomTone> &custom)
{
    return offers(writingTones(custom), id) ? id : QStringLiteral("none");
}

QString offeredCleanupLevel(const QString &id, const QList<CustomCleanupLevel> &custom)
{
    return offers(cleanupStrengths(custom), id) ? id : QStringLiteral("balanced");
}

QString customChoiceId(const QString &name, const QStringList &taken)
{
    QString slug = name.trimmed().toLower();
    for (QChar &character : slug) {
        if (!character.isLetterOrNumber()) {
            character = QLatin1Char('_');
        }
    }
    const QString base = kCustomIdPrefix + slug;
    QString id = base;
    for (int suffix = 2; taken.contains(id); ++suffix) {
        id = base + QStringLiteral("_%1").arg(suffix);
    }
    return id;
}

QString choiceSetText(const CollectionColumn &column, const QStringList &ids, const AppSettings &settings)
{
    QStringList labels;
    for (const RowOption &option : column.options(settings)) {
        if (ids.contains(option.id)) {
            labels.append(option.label);
        }
    }
    return labels.isEmpty() ? column.everyLabel : labels.join(QStringLiteral(", "));
}

QList<RowOption> writingProfileChoices(const QList<WritingProfileSettings> &profiles)
{
    QList<RowOption> options;
    for (const WritingProfileSettings &builtIn : defaultWritingProfileSettings()) {
        options.append({builtIn.profile, writingProfileLabel(builtIn.profile, {})});
    }
    for (const WritingProfileSettings &profile : profiles) {
        if (!isBuiltInWritingProfile(profile.profile)) {
            options.append({profile.profile, profile.name});
        }
    }
    return options;
}

QList<WritingProfileSettings> withCustomProfileIds(QList<WritingProfileSettings> profiles)
{
    QStringList taken;
    for (const WritingProfileSettings &profile : profiles) {
        taken << profile.profile;
    }
    for (WritingProfileSettings &profile : profiles) {
        profile.name = profile.name.trimmed();
        if (profile.profile.isEmpty() && !profile.name.isEmpty()) {
            profile.profile = customChoiceId(profile.name, taken);
            taken << profile.profile;
        }
    }
    return profiles;
}

QString writingProfileDeletionNotice(const AppSettings &settings, const QString &profileId)
{
    const auto rules = std::count_if(settings.appRecognitionRules.cbegin(), settings.appRecognitionRules.cend(),
                                     [&profileId](const AppRecognitionRule &rule) {
                                         return rule.writingProfile == profileId;
                                     });
    QStringList notice;
    if (rules > 0) {
        notice << (rules == 1 ? QStringLiteral("1 application rule uses this profile and will lose it.")
                              : QStringLiteral("%1 application rules use this profile and will lose it.")
                                    .arg(rules));
    }
    if (settings.refinement.defaultWritingProfile == profileId) {
        notice << QStringLiteral("The fallback profile will become Other.");
    }
    return notice.join(QLatin1Char(' '));
}

QString writingProfileChoiceSummary(const AppSettings &settings, const QString &profileId)
{
    const RefinementSettings &refinement = settings.refinement;
    const WritingProfileSettings profile = writingProfileSettingsFor(refinement.writingProfiles, profileId);
    const QString level = offeredCleanupLevel(profile.cleanupStrength, refinement.customCleanupLevels);
    const QString tone = offeredTone(profile.tone, refinement.customTones);
    const auto label = [](const QList<RowOption> &options, const QString &id) {
        return std::find_if(options.cbegin(), options.cend(),
                            [&id](const RowOption &option) { return option.id == id; })->label;
    };
    const QString language = profile.outputLanguage.trimmed();
    const QString refinedLevel = refinedCleanupLevel(level, language);
    // A profile set to None without an output language is not refined, so its
    // tone and instructions do nothing.
    if (refinedLevel == QStringLiteral("none")) {
        return QStringLiteral("No cleanup.");
    }
    QString summary = QStringLiteral("%1 cleanup, ").arg(label(cleanupStrengths(refinement.customCleanupLevels), refinedLevel))
        + (tone == QStringLiteral("none") ? QStringLiteral("no tone.")
                                          : QStringLiteral("%1 tone.").arg(label(writingTones(refinement.customTones), tone)));
    if (!profile.instructions.trimmed().isEmpty()) {
        summary += QStringLiteral(" Has its own instructions.");
    }
    if (!language.isEmpty()) {
        summary += QStringLiteral(" Writes in %1.").arg(language);
    }
    return summary;
}

namespace {

// What a recognition rule's match text reads as in a sentence: each word
// capitalized, except the built-in matches whose names are spelled otherwise.
QString appDisplayName(const QString &match)
{
    static const QHash<QString, QString> spelled{
        // ui-lint: allow title-case: the app's name.
        {QStringLiteral("t3code"), QStringLiteral("T3 Code")},
        {QStringLiteral("chatgpt"), QStringLiteral("ChatGPT")},
        {QStringLiteral("kmail"), QStringLiteral("KMail")},
    };
    if (const auto found = spelled.constFind(match.toLower()); found != spelled.cend()) {
        return *found;
    }
    QStringList words = match.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &word : words) {
        word[0] = word.at(0).toUpper();
    }
    return words.join(QLatin1Char(' '));
}

} // namespace

QString writingProfileSummary(const AppSettings &settings, const QString &profileId)
{
    // The person's rules come first, as they do when Speecher picks a profile.
    QStringList apps;
    for (const AppRecognitionRule &rule :
         recognitionRulesWithMigratedProfileOverrides(settings.appRecognitionRules,
                                                      settings.refinement.writingProfileOverrides)
             + builtInAppRecognitionRules()) {
        const QString name = appDisplayName(rule.match);
        if (rule.writingProfile == profileId && !name.isEmpty()
            && !apps.contains(name, Qt::CaseInsensitive)) {
            apps.append(name);
        }
    }
    QStringList sentences{writingProfileChoiceSummary(settings, profileId)};
    if (apps.size() == 1) {
        sentences << QStringLiteral("Used in %1.").arg(apps.first());
    } else if (apps.size() == 2) {
        sentences << QStringLiteral("Used in %1 and %2.").arg(apps.at(0), apps.at(1));
    } else if (apps.size() > 2) {
        sentences << QStringLiteral("Used in %1, %2 and %3 more.").arg(apps.at(0), apps.at(1)).arg(apps.size() - 2);
    }
    if (offeredWritingProfile(settings.refinement.defaultWritingProfile, settings.refinement.writingProfiles)
        == profileId) {
        sentences << (apps.isEmpty() ? QStringLiteral("Used when no other profile matches.")
                                     : QStringLiteral("Also used when no other profile matches."));
    }
    return sentences.join(QLatin1Char(' '));
}

QString writingProfileDeletionTitle()
{
    return QStringLiteral("Delete profile");
}

CollectionDescriptor writingProfileGrid()
{
    const QString kProfileIdKey = QStringLiteral("profileId");
    const QString kCleanupColumn = QStringLiteral("cleanup");
    const QString kToneColumn = QStringLiteral("tone");
    const QString kInstructionsColumn = QStringLiteral("instructions");
    const QString kOutputLanguageColumn = QStringLiteral("outputLanguage");
    CollectionColumn instructions{kInstructionsColumn, QStringLiteral("Instructions"), ColumnKind::Text, {}, true};
    instructions.multiline = true;
    instructions.placeholder = QStringLiteral("Keep it short and sign off with my first name.");
    CollectionColumn outputLanguage{kOutputLanguageColumn, QStringLiteral("Output language"), ColumnKind::Text};
    outputLanguage.placeholder = QStringLiteral("Same as spoken");
    CollectionDescriptor grid;
    grid.identityColumn = kProfileIdKey;
    grid.columns = {
        // Read-only for the built-ins; a custom profile's name can be edited.
        {kProfileColumn, QStringLiteral("Profile"), ColumnKind::ReadOnly},
        {kCleanupColumn,
         QStringLiteral("Cleanup"),
         ColumnKind::Choice,
         [](const AppSettings &settings) {
             return cleanupStrengths(settings.refinement.customCleanupLevels);
         }},
        {kToneColumn,
         QStringLiteral("Tone"),
         ColumnKind::Choice,
         [](const AppSettings &settings) { return writingTones(settings.refinement.customTones); }},
        instructions,
        // After the instructions: a record dialog takes its first one-line
        // text field for the record's name and requires it.
        outputLanguage,
    };
    // The built-ins always exist, so the stored list only says what each of
    // them was set to; the custom profiles follow in stored order.
    grid.records = [=](const AppSettings &settings) {
        const QList<WritingProfileSettings> &stored = settings.refinement.writingProfiles;
        QList<QVariantMap> records;
        for (const RowOption &profile : writingProfileChoices(stored)) {
            const WritingProfileSettings chosen = writingProfileSettingsFor(stored, profile.id);
            records.append({{kProfileColumn, profile.label},
                            {kProfileIdKey, profile.id},
                            {kCleanupColumn, chosen.cleanupStrength},
                            {kToneColumn, chosen.tone},
                            {kOutputLanguageColumn, chosen.outputLanguage},
                            {kInstructionsColumn, chosen.instructions}});
        }
        return records;
    };
    // A record without an id is a profile just added, which gets one from
    // its name.
    grid.apply = [=](AppSettings &settings, const QList<QVariantMap> &records) {
        QList<WritingProfileSettings> profiles;
        for (const QVariantMap &record : records) {
            const QString id = record.value(kProfileIdKey).toString();
            profiles.append({id,
                             record.value(kCleanupColumn).toString(),
                             record.value(kToneColumn).toString(),
                             record.value(kInstructionsColumn).toString(),
                             isBuiltInWritingProfile(id) ? QString() : record.value(kProfileColumn).toString(),
                             record.value(kOutputLanguageColumn).toString().trimmed()});
        }
        settings.refinement.writingProfiles = withCustomProfileIds(profiles);
    };
    grid.blankRecord = {{kProfileColumn, QStringLiteral("New profile")},
                        {kCleanupColumn, QStringLiteral("balanced")},
                        {kToneColumn, QStringLiteral("none")},
                        {kOutputLanguageColumn, QString()},
                        {kInstructionsColumn, QString()}};
    grid.lockedRecordCount = [] { return int(defaultWritingProfileSettings().size()); };
    grid.addLabel = QStringLiteral("Add profile");
    grid.addDialogTitle = QStringLiteral("New profile");
    return grid;
}

QList<RowOption> authModeOptions(const QString &rowId)
{
    if (rowId == QStringLiteral("openAiAuthMode")) {
        return {
            {QStringLiteral("auto"), QStringLiteral("Automatic")},
            {QStringLiteral("codex_api_key"), QStringLiteral("API key from the Codex app")},
            {QStringLiteral("codex_oauth"), QStringLiteral("ChatGPT sign-in from the Codex app")},
            {QStringLiteral("env"), QStringLiteral("API key from the environment")},
            {QStringLiteral("settings"), QStringLiteral("API key saved in Speecher")},
            {QStringLiteral("cliproxy"), QStringLiteral("CLI Proxy API account")},
        };
    }
    if (rowId == QStringLiteral("anthropicAuthMode")) {
        return {
            {QStringLiteral("oauth"), QStringLiteral("Claude Code sign-in")},
            {QStringLiteral("cliproxy"), QStringLiteral("CLI Proxy API account")},
        };
    }
    return {};
}

AppSettings mergeSettingsDraft(const SettingsSchema &schema, const AppSettings &loaded,
                               const AppSettings &draft, AppSettings current)
{
    const QString endpointKey = SecretStore::settingsKey(SecretStore::Secret::RefinementEndpointKey);
    const QString proxyKey = SecretStore::settingsKey(SecretStore::Secret::CliproxyApiKey);
    const bool clearedUnreadKey = loaded.unreadSecretKeys.contains(endpointKey)
        && !draft.unreadSecretKeys.contains(endpointKey);
    const bool detachedUnreadProxyKey = loaded.unreadSecretKeys.contains(proxyKey)
        && (loaded.refinement.endpoint.preset == QStringLiteral("cliproxy") || loaded.refinement.endpoint.useCliproxyKey)
        && draft.refinement.endpoint.preset.isEmpty() && !draft.refinement.endpoint.useCliproxyKey;
    for (const SettingsPage &page : schema.pages) {
        for (const SettingsSection &section : page.sections) {
            for (const SettingsRow &row : section.rows) {
                if (!row.value || !row.apply) continue;
                const bool editedKey = row.id == QStringLiteral("refinementEndpointApiKey")
                    && (clearedUnreadKey || detachedUnreadProxyKey);
                if (!editedKey && row.value(loaded) == row.value(draft)) continue;
                const CollectionDescriptor &collection = row.collection;
                if (collection.identityColumn.isEmpty()) {
                    row.apply(current, row.value(draft));
                    continue;
                }
                const auto previous = collection.records(loaded);
                const auto latest = collection.records(current);
                auto edited = collection.records(draft);
                const QString &key = collection.identityColumn;
                for (const QVariantMap &old : previous) {
                    const auto same = [&](const QVariantMap &record) { return record.value(key) == old.value(key); };
                    auto change = std::find_if(edited.begin(), edited.end(), same);
                    if (change == edited.end()) continue; // The user deleted it.
                    const auto fresh = std::find_if(latest.cbegin(), latest.cend(), same);
                    if (fresh == latest.cend()) {
                        if (*change == old) edited.erase(change);
                        continue;
                    }
                    for (auto field = fresh->cbegin(); field != fresh->cend(); ++field) {
                        if (change->value(field.key()) == old.value(field.key())) {
                            change->insert(field.key(), field.value());
                        }
                    }
                }
                for (const QVariantMap &fresh : latest) {
                    const auto same = [&](const QVariantMap &record) { return record.value(key) == fresh.value(key); };
                    if (std::none_of(previous.cbegin(), previous.cend(), same)
                        && std::none_of(edited.cbegin(), edited.cend(), same)) edited.append(fresh);
                }
                collection.apply(current, edited);
            }
        }
    }
    return current;
}

} // namespace speecher
