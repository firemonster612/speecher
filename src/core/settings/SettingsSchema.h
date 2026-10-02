#pragma once

#include "core/AppSettings.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

namespace speecher {

enum class RowKind {
    Choice,
    Toggle,
    Text,
    Number,
    Action,
    Info,
    Collection,
    Custom,
};

struct RowOption {
    QString id;
    QString label;
    QString help;
    bool enabled = true;
};

enum class ColumnKind {
    Text,
    Choice,
    Toggle,
    ReadOnly,
};

// One typed column of a collection. A record's value for the column lives under
// `id` in the record, so a key no column names is metadata the editor carries
// but never shows.
struct CollectionColumn {
    QString id;
    QString title;
    ColumnKind kind = ColumnKind::Text;
    // Choice columns only. They may depend on the settings, such as the
    // custom tones a tone column also offers.
    std::function<QList<RowOption>(const AppSettings &)> options;
    // The column that takes the leftover width; the others size to content.
    bool stretch = false;
    // Shown on the cells of this column.
    QString tooltip;
    // Shown instead when what to say depends on the record, such as the
    // confidence behind a learned correction.
    std::function<QString(const QVariantMap &)> recordTooltip;
    // Text columns only: the value may hold several lines, such as a snippet.
    bool multiline = false;
    // Text columns only: an example shown in an empty field.
    QString placeholder;
    // Filled in where a record is added or edited, but left out of the table:
    // a detail that would crowd it, such as what a cleanup level builds on.
    bool dialogOnly = false;
};

// Records a collection can be filled from a file with. Core owns the parse; the
// file chooser and the refusal belong to the front end.
struct CollectionImport {
    QString actionLabel;
    // A name filter, such as "CSV files (*.csv);;All files (*)".
    QString fileFilter;
    // What a front end titles a refusal, whether the parse or the merge failed.
    QString failureTitle;
    // Leaves error empty when every record in the file is usable.
    std::function<QList<QVariantMap>(const QByteArray &, QString *error)> parse;
};

// A table of records with typed columns, plus add and delete. Five surfaces in
// this app are this shape, so describing it once is what lets a front end reach
// for its own table view instead of reimplementing the editor.
struct CollectionDescriptor {
    // Stable identity for collections also updated by correction learning.
    QString identityColumn;
    QList<CollectionColumn> columns;
    std::function<QList<QVariantMap>(const AppSettings &)> records;
    // Receives the editable records only, never the locked ones.
    std::function<void(AppSettings &, const QList<QVariantMap> &)> apply;
    QVariantMap blankRecord;
    // Leading records a reader can see but nobody can edit or delete, such as
    // the built-in application recognition rules.
    std::function<int()> lockedRecordCount;
    // Empty when the records are consistent; otherwise one message per problem,
    // ready to show to a person.
    std::function<QStringList(const QList<QVariantMap> &)> validate;
    // Empty on a collection nothing may be added to by hand.
    QString addLabel;
    // The title of the dialog or sheet that fills in a new record, where a
    // front end adds through one.
    QString addDialogTitle;
    // The command that deletes records, the selection's or one row's. There is
    // no confirmation: every editor can undo a delete.
    QString deleteLabel = QStringLiteral("Delete");
    // What the editor says while it holds no records, in the collection's own
    // terms. Empty on a collection that always has records (built-ins).
    QString emptyTitle;
    QString emptyHelp;
    // Set when the collection can also be filled from a file.
    CollectionImport supportsImport;
    // Commands beyond add and delete. The schema names them so a second front
    // end can offer the same ones; what they do stays with the front end,
    // because both of today's two undo its own edit history.
    QList<RowOption> actions;
    int minimumHeight = 0;
    // A short label shown as a pill beside a record's stretch column, one per
    // record in order, empty for none: which vocabulary terms the speech
    // service receives. It depends on the other records, so a front end asks
    // again with its current records each time it redraws them.
    std::function<QStringList(const QList<QVariantMap> &, const AppSettings &)> badges;
};

struct NumberRange {
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    QString suffix;
};

// What the machine Speecher is running on can do, for rows that are only
// meaningful when it can. Grows a member when a row needs one, not before.
struct Capabilities {
    bool targetAccessibility = false;
    bool automaticUpdateDownloads = false;
    // The platform theme honours a Light or Dark request. Assumed until a
    // request is seen to be ignored.
    bool colorSchemeOverride = true;
    // This computer took the last launch-at-login change. Assumed until one is
    // refused, which is the only thing the caution beside the toggle reports.
    bool launchAtLoginAccepted = true;
    // The system lists at least one microphone. Assumed until the front end's
    // device list comes back empty.
    bool audioInput = true;
};

// An expert setting kept off the page: the card shows a button row in its
// place, titled and described as here, that opens the setting in a dialog of
// the same title.
struct RowDialog {
    QString title;
    // The button row's description, which says what the setting holds now.
    std::function<QString(const AppSettings &)> summary;
};

struct SettingsRow {
    // Stable across front ends: a renderer uses it to name its control, and a
    // Custom or Action row is recognised by it.
    QString id;
    // First Stable Release that ships this row. Empty for rows that predate
    // release-note discovery or should not appear as something new.
    QString sinceVersion;
    QString label;
    // Replaces label when what the row holds depends on the settings, such as
    // a sign-in status that becomes an API key field.
    std::function<QString(const AppSettings &)> labelValue;
    QString help;
    std::function<QString(const AppSettings &)> helpValue;
    RowKind kind = RowKind::Info;
    // The caption of an Action row's control, which is not its label.
    QString actionLabel;
    // Replaces actionLabel when the click does different things in different
    // states, so the caption always says what it will do.
    std::function<QString(const AppSettings &)> actionLabelValue;
    NumberRange range;
    // Room to reserve for a Choice row's value, in characters, so a list that
    // arrives late does not resize the row under the reader. Zero sizes the
    // control to whatever it holds.
    int contentWidthHint = 0;
    // Shown on the control itself, where help sits beneath the label.
    QString tooltip;
    // Replaces tooltip while enabled says no. A front end shows it beside the
    // disabled control, not only on hover.
    QString disabledHelp;
    // Replaces disabledHelp where the schema can tell which gate is closed, so
    // a row held by more than one names only that one.
    std::function<QString(const AppSettings &, const Capabilities &)> disabledHelpValue;
    // An action a front end can run to lift the gate, with the caption of the
    // control that runs it. Empty when nothing in the app can.
    QString disabledAction;
    QString disabledActionLabel;
    // Names a gate that many rows share, such as desktop accessibility, so a
    // front end can explain it once for the page rather than on every row,
    // whether or not an action can lift it.
    QString sharedGate;
    // Rows that name the same group render inside one container and are enabled
    // or disabled together, so they must all declare the same gate.
    QString groupId;
    // Collection metadata, also available on Custom rows with native table rendering.
    CollectionDescriptor collection;
    // Set on a row shown in a dialog rather than on the page. Adjacent rows
    // with the same dialog title share one dialog and one button row.
    RowDialog dialog;
    std::function<QVariant(const AppSettings &)> value;
    std::function<void(AppSettings &, const QVariant &)> apply;
    std::function<QList<RowOption>(const AppSettings &)> options;
    // Text rows only: values worth offering, though the row still takes any
    // text a person types.
    std::function<QList<RowOption>(const AppSettings &)> suggestions;
    // Text rows only: a key or password, shown masked.
    bool secret = false;
    // Text rows, and Custom rows that take text: shown in the empty field.
    QString placeholder;
    // Text rows only: the value may hold several lines.
    bool multiline = false;
    // A Collection row that is not enabled stays readable: its records show
    // and scroll, and only editing, adding and deleting stop.
    std::function<bool(const AppSettings &, const Capabilities &)> enabled;
    // A row that is only worth showing sometimes, such as a caution about the
    // model currently chosen. Absent means always.
    std::function<bool(const AppSettings &, const Capabilities &)> visible;
    // Populating this row reaches for something slow — a device enumeration, a
    // keyring — so a front end leaves it until it has painted once.
    bool expensive = false;
};

struct SettingsSection {
    // The heading every front end shows above the section's card, which is
    // also the title of the pane group that shows it. Empty only for What's
    // New's release notes, which need none.
    QString title;
    // A footnote below the section's rows.
    QString help;
    QList<SettingsRow> rows;
};

// Where rows are defined, not where they are shown: the panes below decide
// that. A schema page groups rows that are built from the same context.
struct SettingsPage {
    QString id;
    QList<SettingsSection> sections;
};

// What a pane's groups are to each other.
enum class PaneLayout {
    // Sections of one page, shown together.
    Sections,
    // Views of one idea, one at a time, chosen with a segmented control.
    Alternatives,
    // Transcribing audio files, which has no schema rows behind it either.
    Transcribe,
    // Home: the dictation card and insights, drawn by each front end from
    // InsightsSummary rather than from schema rows.
    Home,
};

// One card a pane shows: the schema section settingsPanes() names for it,
// with that section's title and footnote, so a heading reads the same on every
// platform and cannot drift from the section it shows.
struct SettingsPaneGroup {
    // An Alternatives pane's view, addressed as "pane:view"; empty otherwise.
    QString view;
    QString title;
    QString help;
    // Schema row ids, in the order they should read, as this build has them.
    QStringList rows;
};

// One sidebar entry of the settings window. Linux, macOS and Windows all read
// the panes and runs below to build their sidebars and pages: the schema's
// pages supply rows and values, and which pane a row appears on is decided
// here (docs/adr/0001-per-platform-front-ends.md).
struct SettingsPane {
    QString id;
    QString title;
    // Platform-neutral; each front end maps it to an icon of its own.
    QString iconId;
    PaneLayout layout = PaneLayout::Sections;
    QList<SettingsPaneGroup> groups;
    // One line under the page title saying what the page is for, or empty.
    QString intro;
};

// One titled run of the sidebar, as System Settings groups its pages under a
// header. The first group has no title, like System Settings' Quick Settings.
struct SidebarGroup {
    QString title;
    QStringList panes;

    bool operator==(const SidebarGroup &) const = default;
};

struct SettingsSchema {
    // localModels is among them only when speechProviders offers "local",
    // and so is its pane.
    QList<SettingsPage> pages;
    QList<SettingsPane> panes;
    // The sidebar's groups, in order. A pane in no group (What's New) appears
    // only while pending or selected, first in the untitled top group.
    QList<SidebarGroup> sidebarGroups;

    const SettingsPage &page(const QString &id) const;
    bool hasPage(const QString &id) const;
    // Null for an id this schema does not have.
    const SettingsPane *pane(const QString &id) const;
    const SettingsRow *row(const QString &id) const;
    // A group as a section of the schema's rows, ready to render.
    SettingsSection section(const SettingsPaneGroup &group) const;
};

// Every page a front end can show is named by one id: a pane id, or
// "pane:view" for one view of an Alternatives pane (vocabulary:corrections).
// The same ids serve SPEECHER_GRAB_PAGE, links between pages and notification
// targets. A window opened from hidden shows Home; "Settings…" shows General.
struct PageId {
    QString pane;
    QString view;
};
// Case-insensitive. A bare Alternatives pane id gives its first view. Ids of
// panes since merged into others (shortcut, apps) give the pane that holds
// their settings now. An unknown pane, or a view the pane does not have, warns
// and gives Home.
PageId resolvePage(const SettingsSchema &schema, const QString &request);
// The panes a sidebar search shows, in sidebar order: those whose title,
// group titles, or visible rows' labels or help mention the query. Rows are
// visible as settings would show them. What's New never matches; an empty
// query matches every pane in a group.
QStringList searchPanes(const SettingsSchema &schema, const QString &query, const AppSettings &settings,
                        const Capabilities &capabilities);
// One pane searchPanes finds, with the visible rows whose label or help
// mention the query, in reading order. Empty rows means the pane matched by
// its title or a group's title or footnote.
struct SearchMatch {
    QString pane;
    QStringList rows;
};
QList<SearchMatch> searchSettings(const SettingsSchema &schema, const QString &query, const AppSettings &settings,
                                  const Capabilities &capabilities);

// What help and error text calls a page, so a sentence that sends someone to
// one names a page that exists. These read the arrangement every build shares.
QString paneTitle(const QString &paneId);
QString paneTitleForRow(const QString &rowId);

// A refinement provider as the settings surface sees it: what to call it, and
// what it can be asked to do.
struct RefinementProvider {
    QString id;
    QString label;
    bool supportsScreenshotContext = false;
};

// A graphics card as one transcribe.cpp backend reports it.
struct LocalGpu {
    // A localBackends kind other than "auto" or "cpu".
    QString backend;
    // The PCI bus id, empty when the backend reports none.
    QString deviceId;
    QString description;
};

// What the app layer last learned about this computer and the servers a
// person named, for the rows that report it. ApplicationController's
// LocalSetup builds it; a front end hands the schema a way to read it, runs the
// actions below, and refreshes the rows when LocalSetup says changed().
//
// Actions a front end runs for these rows, by row id:
// - speechEndpointTest: LocalSetup::checkSpeechEndpoint(draft.speech.endpoint)
// - refinementEndpointTest: LocalSetup::checkRefinementEndpoint(draft.refinement)
// - localRunnerDetect: LocalSetup::detectRunners()
// - localModelFolder: open LocalModelStore::directory() in the file manager
// - speechLocalModelDownload: show the Local models page (a front-end job)
struct LiveFacts {
    // The last connection check's verdict, "Checking…" while one runs, empty
    // before the first. LocalSetup runs one on its own once an endpoint in
    // use is filled in, as well as for Test connection.
    QString speechEndpointStatus;
    QStringList speechEndpointModels;
    QString refinementEndpointStatus;
    QStringList refinementEndpointModels;
    // Local Runners found on this computer: id as LocalRunnerSettings stores
    // it, label with the version.
    QList<RowOption> runners;
    // What each found runner can serve, by runner id.
    QHash<QString, QStringList> runnerModels;
    bool detectingRunners = false;
    // Where Local Model files live and how much room they take.
    QString modelFolder;
    // Local Model ids with a finished download, in catalog order.
    QStringList downloadedModels;
    // The graphics cards a Local Model can run on, once per backend that
    // reaches them.
    QList<LocalGpu> localGpus;
    // Where the loaded Local Model runs, e.g. "NVIDIA GeForce RTX 3060
    // (CUDA)"; empty while none is loaded.
    QString localModelRunsOn;
};

// What the descriptors need to be built. A value type, so a test can make one
// without a registry, a sound server or a window.
struct SchemaContext {
    QList<RowOption> speechProviders;
    QList<RefinementProvider> refinementProviders;
    std::function<QList<RowOption>()> audioInputDevices;
    // This build can set up a virtual keyboard, so the Output page carries the
    // row that drives it.
    bool virtualKeyboardSetup = false;
    QString currentVersion;
    QString lastSeenVersion;
    // Absent reads as nothing learned yet.
    std::function<LiveFacts()> liveFacts;
    // Endpoint verdicts must match the draft currently on screen.
    std::function<LiveFacts(const AppSettings &)> liveFactsForDraft;
    // builtInDictationSystemPrompt(), which the custom system prompt editor
    // shows while nothing is stored. The prompt lives with the providers.
    QString builtInSystemPrompt;
};

// What refinement does, in one sentence: the Refinement page's intro and the
// setup assistant's Refinement step open with it.
QString refinementIntro();

// The built-in cleanup levels, then the custom ones. Each option's help says
// what the level does, for a front end to show under the chosen one.
QList<RowOption> cleanupStrengths(const QList<CustomCleanupLevel> &custom);
// No tone and the built-in tones, then the custom ones, each with
// its instruction as help.
QList<RowOption> writingTones(const QList<CustomTone> &custom);
// The id when it is offered, otherwise what a profile whose choice was deleted
// falls back to: no tone override, or Medium.
QString offeredTone(const QString &id, const QList<CustomTone> &custom);
QString offeredCleanupLevel(const QString &id, const QList<CustomCleanupLevel> &custom);
// The id a new custom tone, cleanup level or profile named `name` gets: custom_ and the
// name in lowercase with every other character made _, so it never matches a
// built-in id, then _2, _3 and so on until it is none of `taken`.
QString customChoiceId(const QString &name, const QStringList &taken);
// The built-in profiles, then the custom ones `profiles` holds.
QList<RowOption> writingProfileChoices(const QList<WritingProfileSettings> &profiles);
// What a profile does, in a sentence or two: its cleanup and tone, and
// whether it adds instructions of its own. "Medium cleanup, no tone."
QString writingProfileChoiceSummary(const AppSettings &settings, const QString &profileId);
// The same, then where Speecher uses it: the apps the recognition rules map
// to it, and whether it is the fallback. The profile's row on the Writing
// Profiles page reads this.
QString writingProfileSummary(const AppSettings &settings, const QString &profileId);
// Each named profile without an id, one just added, gets customChoiceId of
// its name.
QList<WritingProfileSettings> withCustomProfileIds(QList<WritingProfileSettings> profiles);
// What deleting a profile changes, for the person deleting it to read first:
// how many application rules lose it, and whether the fallback profile becomes
// Other. Empty when nothing points at it.
QString writingProfileDeletionNotice(const AppSettings &settings, const QString &profileId);
// The title of the confirmation that notice goes in; its confirming button is
// the grid's deleteLabel.
QString writingProfileDeletionTitle();
CollectionDescriptor writingProfileGrid();
QList<RowOption> authModeOptions(const QString &rowId);

// Apply only edits since the loaded snapshot, keeping newer store values.
AppSettings mergeSettingsDraft(const SettingsSchema &schema, const AppSettings &loaded,
                               const AppSettings &draft, AppSettings current);

SettingsSchema buildSettingsSchema(const SchemaContext &context);

// Nightly metadata does not make a new Stable Release, and dotted components
// are numbers rather than text (0.10 follows 0.2).
int compareBaseVersions(const QString &left, const QString &right);

// The nightly What's New body: the first-parent commits between two nightly
// builds, read from the build-embedded history (0x1e-separated records of
// sha, subject, body), each bullet linking its pull request or commit, ending
// in a GitHub compare link. Falls back to the compare link alone when the
// previous commit is outside the embedded window, and to empty unless the
// current version is a nightly and both versions name a commit.
QString nightlyChangesMarkdown(const QString &history, const QString &lastVersion,
                               const QString &currentVersion);

// The one sentence that describes the restore-clipboard setting, wherever it
// is offered (Output page, setup assistant).
QString restoreClipboardDescription();

// What the OpenAI sign-in row says. The split between what an API key buys and
// what dictation needs decides whether dictation works at all, so it is visible
// help on every platform rather than something only a hover reveals.
QString openAiSignInHelp();

// The fast-mode help and tooltip for a refinement provider ("openai" or
// "anthropic"), rendered by the schema rows and by the setup assistants'
// own fast-mode checkboxes. Both must say the same thing.
QString fastModeHelp(const QString &refinementProviderId);
QString fastModeTooltip(const QString &refinementProviderId);
// The OpenAI Speed row's help and choices, which the setup assistants share.
QString openAiSpeedHelp();
QList<RowOption> openAiSpeedOptions(const QString &model);

// Where a key the settings surface takes is kept, as a row's help says it.
QString keyStorageHelp();

// One sentence naming the platform's accessibility feature and what it
// unlocks, "Turn on desktop accessibility to learn corrections."
QString accessibilityGateHelp(const QString &purpose);
// The caption of the control that asks for the accessibility grant, wherever
// it is offered: gated rows, the shortcut recorder, and through
// accessibilityActionCaption Home and setup.
QString accessibilityGrantActionLabel();

// What a credential status says while it is being resolved.
QString checkingCredentialsStatus();

// What a Local Runner status says while Speecher looks for one, in settings
// and in setup.
QString lookingForRunnersStatus();

// What the Global Shortcut row says while it waits for keys, naming this
// platform's keys.
QString globalShortcutPrompt();
// The Global Shortcut row's buttons: record a new binding, and go back to the
// binder's default, named by its display text.
QString globalShortcutChangeCaption();
QString globalShortcutResetCaption(const QString &defaultShortcut);
// What the Global Shortcut row shows in place of an empty binding.
QString globalShortcutUnsetText();
// Setup's shortcut step: record the first binding, record a single key where
// the desktop registers no combinations, let the desktop pick one, and remove
// the binding.
QString globalShortcutSetCaption();
QString globalShortcutSingleKeyCaption();
QString globalShortcutChooseCaption();
QString globalShortcutClearCaption();
// What the Global Shortcut row says when the binder refused a binding without
// saying why.
QString globalShortcutBindFailedText();

// What settings search shows when nothing matches the query.
QString noSettingsMatchText();

// The microphone choice as it is offered: a system-default entry ahead of the
// devices that exist, and a disabled placeholder standing in for a saved device
// that has gone away. Shared with the setup assistant's own device list.
QList<RowOption> audioDeviceOptions(const QList<RowOption> &devices,
                                    const QString &selectedDeviceId);
// The system-default entry's label.
QString audioDeviceDefaultLabel();
// The caption of the button on the Test microphone row, which says what a
// click does now.
QString microphoneTestCaption(bool running);

// The Local models page's Acceleration choice: Automatic, the CPU, then each
// GPU backend that reaches a card here, with a disabled placeholder for a
// saved backend this computer lacks. Option ids are localBackends kinds.
QList<RowOption> localAccelerationOptions(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen);
// Its Graphics card choice: each card the chosen backend reaches, by device
// id, with a disabled placeholder for a saved card that has gone. Empty for
// the CPU and plain Automatic, which pick no card.
QList<RowOption> localGraphicsCardOptions(const QList<LocalGpu> &gpus, const LocalRunsOn &chosen);

} // namespace speecher
