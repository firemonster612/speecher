#pragma once

#import <Foundation/Foundation.h>

// The whole of Speecher's C++ core as Swift sees it. Swift never includes a C++
// header: the settings descriptors' std::function members stay on the other
// side of this wall and arrive here already flattened into value objects, so no
// C++ interop mode is needed. See docs/adr/0001-per-platform-front-ends.md.
//
// This header is also the Swift target's bridging header, so it must stay
// compilable as plain Objective-C. The initialisers that take C++ types are
// declared under __cplusplus, which the Swift importer never defines.

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, SpeecherRowKind) {
    SpeecherRowKindChoice,
    SpeecherRowKindToggle,
    SpeecherRowKindText,
    SpeecherRowKindNumber,
    SpeecherRowKindAction,
    SpeecherRowKindInfo,
    SpeecherRowKindCollection,
    SpeecherRowKindCustom,
};

typedef NS_ENUM(NSInteger, SpeecherColumnKind) {
    SpeecherColumnKindText,
    SpeecherColumnKindChoice,
    SpeecherColumnKindToggle,
    SpeecherColumnKindReadOnly,
};

// One record of a collection: column ids to values, plus the keys no column
// shows, which an editor carries through an edit untouched.
typedef NSDictionary<NSString *, id> SpeecherRecord;

@interface RowOptionModel : NSObject
@property (nonatomic, readonly, copy) NSString *rowOptionId;
@property (nonatomic, readonly, copy) NSString *label;
// Why a disabled choice cannot be picked.
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly) BOOL enabled;
@end

@interface CollectionColumnModel : NSObject
@property (nonatomic, readonly, copy) NSString *columnId;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly) SpeecherColumnKind kind;
// Choice columns only.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *options;
// The column that takes the leftover width; the others size to content.
@property (nonatomic, readonly) BOOL stretch;
@end

// A table of records with typed columns. Everything about it that does not
// change with the records, so an editor can build its columns and buttons once.
@interface CollectionModel : NSObject
@property (nonatomic, readonly, copy) NSArray<CollectionColumnModel *> *columns;
// Leading records a reader can see but nobody can edit or delete.
@property (nonatomic, readonly) NSInteger lockedRecordCount;
@property (nonatomic, readonly, copy) SpeecherRecord *blankRecord;
// Empty on a collection nothing may be added to by hand.
@property (nonatomic, readonly, copy) NSString *addLabel;
// Empty unless the collection can also be filled from a file.
@property (nonatomic, readonly, copy) NSString *importLabel;
@property (nonatomic, readonly, copy) NSArray<NSString *> *importFileExtensions;
// Commands beyond add and delete, which the editor implements itself.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *actions;
@property (nonatomic, readonly) NSInteger minimumHeight;
@end

@interface SettingsRowModel : NSObject
@property (nonatomic, readonly, copy) NSString *rowId;
@property (nonatomic, readonly, copy) NSString *label;
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly) SpeecherRowKind kind;
// The caption of an Action row's button, which is not its label.
@property (nonatomic, readonly, copy) NSString *actionLabel;
@property (nonatomic, readonly) NSInteger minimum;
@property (nonatomic, readonly) NSInteger maximum;
@property (nonatomic, readonly) NSInteger step;
@property (nonatomic, readonly, copy) NSString *suffix;
// An NSString for a Choice, Text or Info row, an NSNumber for a Toggle or
// Number row, an array of records for a Collection row, and nil for a row that
// holds no value of its own.
@property (nonatomic, readonly, strong, nullable) id value;
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *options;
// Text rows only: values worth offering, though the row still takes any text.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *suggestions;
@property (nonatomic, readonly) BOOL enabled;
// Shown on the control, and replaced by disabledHelp while enabled says no.
@property (nonatomic, readonly, copy) NSString *tooltip;
@property (nonatomic, readonly, copy) NSString *disabledHelp;
// The action that can lift the gate while enabled says no — an action id the
// row dispatch understands — and its button caption. Empty when none.
@property (nonatomic, readonly, copy) NSString *disabledAction;
@property (nonatomic, readonly, copy) NSString *disabledActionLabel;
// Set on a Collection row, and on the one Custom row that is a table.
@property (nonatomic, readonly, strong, nullable) CollectionModel *collection;
// Text rows only: a key or password, shown masked.
@property (nonatomic, readonly) BOOL secret;
@end

@interface SettingsSectionModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly, copy) NSArray<SettingsRowModel *> *rows;
@end

@interface SettingsPageModel : NSObject
@property (nonatomic, readonly, copy) NSString *pageId;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *symbolName;
@property (nonatomic, readonly, copy) NSArray<SettingsSectionModel *> *sections;
@end

// What a pane's groups are to each other. Mirrors speecher::PaneLayout.
typedef NS_ENUM(NSInteger, SpeecherPaneLayout) {
    SpeecherPaneLayoutSections,
    SpeecherPaneLayoutAlternatives,
    SpeecherPaneLayoutShortcut,
};

// One card a pane shows: a heading, a footnote, and the schema rows it names.
// A row pattern ending in `*` takes every row whose id starts with it.
@interface SettingsPaneGroupModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly, copy) NSArray<NSString *> *rows;
@end

// One sidebar entry of the settings window, as the schema arranges them.
@interface SettingsPaneModel : NSObject
@property (nonatomic, readonly, copy) NSString *paneId;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *symbolName;
// Schema pages whose otherwise-unmapped rows fall back to this pane.
@property (nonatomic, readonly, copy) NSArray<NSString *> *schemaPages;
@property (nonatomic, readonly) SpeecherPaneLayout layout;
@property (nonatomic, readonly, copy) NSArray<SettingsPaneGroupModel *> *groups;
@end

// What a file offered to a collection turned out to hold.
@interface CollectionImportResult : NSObject
// The records already there with the file's merged in, or nil when the file
// could not be used.
@property (nonatomic, readonly, copy, nullable) NSArray<SpeecherRecord *> *records;
// Empty when the file was usable, and ready to show to a person otherwise.
@property (nonatomic, readonly, copy) NSString *problem;
@end

// The settings surface as the schema describes it, over a draft of the stored
// settings. Reading `pages` re-derives every row's value, choices and enabled
// flag from the draft, so a reader sees the effect of its own writes.
@interface SettingsSchemaModel : NSObject
@property (nonatomic, readonly, copy) NSArray<SettingsPageModel *> *pages;
// The sidebar's panes and their runs, which never change while the app runs.
@property (nonatomic, readonly, copy) NSArray<SettingsPaneModel *> *panes;
@property (nonatomic, readonly, copy) NSArray<NSArray<NSString *> *> *sidebarRuns;
// What an Action row's button does. The schema names the commands; what they do
// belongs to the front end, as it does on Qt.
@property (nonatomic, copy, nullable) void (^actionTriggered)(NSString *rowId);
- (void)setValue:(nullable id)value forRowId:(NSString *)rowId;
// Discards edits left from the last showing and re-reads the store.
- (void)reloadDraft;
// Writes the draft back to the store and re-reads it.
- (void)commit;
// Lets the rows whose choices are slow to gather — a device enumeration —
// offer them from now on. Called once the window has painted.
- (void)loadExpensiveRows;
// Empty when these records are consistent; otherwise one message per problem.
- (NSArray<NSString *> *)problemsWith:(NSArray<SpeecherRecord *> *)records forRowId:(NSString *)rowId;
// Save against the records this editor last submitted, not a later model refresh.
- (NSArray<NSString *> *)saveRecords:(NSArray<SpeecherRecord *> *)records
                    previousRecords:(NSArray<SpeecherRecord *> *)previous
                           forRowId:(NSString *)rowId
    NS_SWIFT_NAME(save(records:previousRecords:forRowId:));
- (CollectionImportResult *)recordsImportedFrom:(NSData *)data
                                           into:(NSArray<SpeecherRecord *> *)records
                                       forRowId:(NSString *)rowId;
// What a cell says on hover, which a learned correction answers per record.
- (NSString *)tooltipForColumn:(NSString *)columnId
                      inRowId:(NSString *)rowId
                       record:(SpeecherRecord *)record;
@end

// The update flow as the banners see it. Mirrors speecher::UpdateController::State.
typedef NS_ENUM(NSInteger, SpeecherUpdateState) {
    SpeecherUpdateStateIdle,
    SpeecherUpdateStateChecking,
    SpeecherUpdateStateCheckFailed,
    SpeecherUpdateStateUpToDate,
    SpeecherUpdateStateUpdateAvailable,
    SpeecherUpdateStateDownloading,
    SpeecherUpdateStateReadyToRestart,
    SpeecherUpdateStateRestartPending,
    SpeecherUpdateStateRestarting,
    SpeecherUpdateStateError,
};

// One provider the setup assistant lists, as the registry descriptor names it.
// The assistant renders these strings rather than keeping copies of its own.
@interface SpeecherProviderModel : NSObject
@property (nonatomic, readonly, copy) NSString *providerId;
@property (nonatomic, readonly, copy) NSString *label;
// The sign-in this provider reads, named the way a person would go looking for
// it ("Claude Code"), which is not the transcription brand.
@property (nonatomic, readonly, copy) NSString *credentialSource;
// What a person must do before this provider works, or empty when it needs
// nothing.
@property (nonatomic, readonly, copy) NSString *setupHint;
@end

// One Local Model as the Local models page and the setup assistant show it:
// the catalog's facts in core's words, and where this computer stands with it.
@interface LocalModelInfo : NSObject
@property (nonatomic, readonly, copy) NSString *modelId;
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly, copy) NSString *fileName;
// "731 MB", as download sizes are quoted.
@property (nonatomic, readonly, copy) NSString *sizeText;
// Word error rates in percent.
@property (nonatomic, readonly) double librispeechWer;
@property (nonatomic, readonly) double fleursWer;
// Text appears as the person speaks rather than after they stop.
@property (nonatomic, readonly) BOOL streams;
@property (nonatomic, readonly, copy) NSString *licence;
@property (nonatomic, readonly, copy) NSArray<NSString *> *pros;
@property (nonatomic, readonly, copy) NSArray<NSString *> *cons;
// "Fits", "Tight fit", "Too large", or "Checking…" before the hardware is known.
@property (nonatomic, readonly, copy) NSString *fitLabel;
@property (nonatomic, readonly) BOOL tooLarge;
// The model Speecher suggests for this computer, once it knows the hardware.
@property (nonatomic, readonly) BOOL suggested;
@property (nonatomic, readonly) BOOL speedTestRunning;
// The rest is LocalSetup::ModelState: how long 10 s of speech takes here as a
// table cell ("~0.7 s (estimated)") and as a sentence, the files, and the
// last download or Speed Test failure (empty when there is none).
@property (nonatomic, readonly, copy) NSString *speedText;
@property (nonatomic, readonly, copy) NSString *speedDetail;
@property (nonatomic, readonly) BOOL downloaded;
@property (nonatomic, readonly) BOOL downloading;
@property (nonatomic, readonly) BOOL inUse;
@property (nonatomic, readonly, copy) NSString *problem;
// While downloading: 0 to 1, and "164 MB of 199 MB".
@property (nonatomic, readonly) double downloadFraction;
@property (nonatomic, readonly, copy) NSString *progressText;
@end

// A Local Runner found on this computer.
@interface LocalRunnerInfo : NSObject
// "ollama", "lmstudio" or "llama-server", as the localRunner row stores it.
@property (nonatomic, readonly, copy) NSString *runnerId;
@property (nonatomic, readonly, copy) NSString *name;
// Empty where the runner does not report one.
@property (nonatomic, readonly, copy) NSString *version;
@property (nonatomic, readonly, copy) NSArray<NSString *> *models;
@end

// The cleanup model Speecher suggests pulling into Ollama.
@interface CleanupModelInfo : NSObject
@property (nonatomic, readonly, copy) NSString *ollamaTag;
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly, copy) NSString *sizeText;
@end

// The last cleanup model pull through Ollama.
@interface CleanupPullInfo : NSObject
@property (nonatomic, readonly) BOOL running;
@property (nonatomic, readonly) double fraction;
// "1.2 GB of 5.3 GB" while running.
@property (nonatomic, readonly, copy) NSString *progressText;
@property (nonatomic, readonly, copy) NSString *error;
@end

// Which Local Runner refinement uses, resolved by core (RunnerChoice): the
// saved runner and model, the running runner they name if any, and whether to
// offer the suggested cleanup model.
@interface LocalRunnerChoice : NSObject
@property (nonatomic, readonly, copy) NSString *runnerId;
// "Ollama", for a saved runner that is not running.
@property (nonatomic, readonly, copy) NSString *runnerName;
@property (nonatomic, readonly, copy) NSString *model;
@property (nonatomic, readonly, strong, nullable) LocalRunnerInfo *available;
@property (nonatomic, readonly) BOOL offerPull;
@property (nonatomic, readonly) BOOL showSuggestion;
@end

// The welcome step's path, owned by core's WelcomeChoice. One per assistant,
// kept across Back and Continue.
@interface SetupWelcomeChoice : NSObject
// After a round of sign-in checks (choice nil) or a click on a path (choice
// set). Returns the speech provider to save.
- (NSString *)updateWithProvider:(NSString *)provider
                  readyProviders:(NSArray<NSString *> *)readyProviders
               proxyAccountFound:(BOOL)proxyAccountFound
                          choice:(nullable NSNumber *)choice
    NS_SWIFT_NAME(update(provider:readyProviders:proxyAccountFound:choice:));
// An explicit Transcription choice, which a later path default must not undo.
- (void)providerChosen;
@property (nonatomic, readonly) BOOL local;
@end

// Everything LocalSetup knows, read in one go on every change it announces.
@interface LocalSetupState : NSObject
// "Apple M4 Max, 14 threads · Apple M4 Max, Metal · 36 GB memory", or
// "Checking this computer…" until the probe answers.
@property (nonatomic, readonly, copy) NSString *hardwareLine;
@property (nonatomic, readonly) BOOL hardwareKnown;
// The catalog, in the order it lists the models.
@property (nonatomic, readonly, copy) NSArray<LocalModelInfo *> *models;
// The Local Model dictation will use: the chosen one, else the suggestion.
@property (nonatomic, readonly, copy) NSString *speechModelChoice;
@property (nonatomic, readonly, strong) LocalRunnerChoice *runnerChoice;
@property (nonatomic, readonly) BOOL detectingRunners;
// nil when cleanup here would be slower than a cloud provider or none.
@property (nonatomic, readonly, strong, nullable) CleanupModelInfo *cleanupModel;
@property (nonatomic, readonly, strong) CleanupPullInfo *pull;
// The refinement endpoint's last verdict for the saved settings, "Checking…"
// while one runs, empty before the first; and the models it listed.
@property (nonatomic, readonly, copy) NSString *endpointStatus;
@property (nonatomic, readonly, copy) NSArray<NSString *> *endpointModels;
@end

// The refinement Custom Endpoint as the setup assistant's form shows it: with
// the CLI Proxy API preset, the server and key are the proxy's.
@interface RefinementEndpointForm : NSObject
// "openai" or "anthropic".
@property (nonatomic, readonly, copy) NSString *format;
@property (nonatomic, readonly, copy) NSString *serverUrl;
@property (nonatomic, readonly, copy) NSString *apiKey;
@property (nonatomic, readonly, copy) NSString *model;
@end

@interface SpeecherBridge : NSObject
@property (nonatomic, readonly, strong) SettingsSchemaModel *settingsSchema;
@property (nonatomic, readonly, copy) NSString *stateName;
@property (nonatomic, copy, nullable) void (^statusChanged)(NSString *status);
@property (nonatomic, copy, nullable) void (^audioLevelChanged)(float level);
// A capability the schema gates rows on moved — the Accessibility grant, or
// whether this computer took the last launch-at-login change — so the settings
// pages are worth re-reading.
@property (nonatomic, copy, nullable) void (^accessibilityChanged)(void);
@property (nonatomic, readonly) BOOL whatsNewPending;
@property (nonatomic, copy, nullable) void (^whatsNewChanged)(void);
- (void)clearPendingWhatsNew;

// The update flow, which the settings banner and the panel's update chip draw.
@property (nonatomic, readonly) SpeecherUpdateState updateState;
// The version an update offers. Empty while none does.
@property (nonatomic, readonly, copy) NSString *updateVersion;
// The running version, whose bare number the what's-new offers show.
@property (nonatomic, readonly, copy) NSString *installedVersion;
@property (nonatomic, readonly) NSInteger updatePercent;
@property (nonatomic, readonly, copy) NSString *updateError;
@property (nonatomic, readonly) BOOL updateBannerVisible;
// The offered stable release replaces a running nightly build.
@property (nonatomic, readonly) BOOL updateStableReplacement;
@property (nonatomic, copy, nullable) void (^updateChanged)(void);
// One click through the whole tail of the flow: download if needed, install,
// then restart. A restart requested mid-dictation waits for the session to end.
- (void)installUpdateAndRestart;
// The current state's single step: download an offered update, restart a ready
// one, or retry a failed check.
- (void)updateNow;
// Hides the offered version until a newer one appears; clears an error banner.
- (void)dismissUpdate;
- (void)toggle;
- (void)startListening;
- (void)stopListening;

// The dictation panel's own state. It is a floating window rather than a
// settings pane, so it reads these rather than the schema.
@property (nonatomic, copy, nullable) void (^popupShowRequested)(uint64_t generation);
@property (nonatomic, copy, nullable) void (^popupHideRequested)(void);
@property (nonatomic, copy, nullable) void (^popupStatusChanged)(NSString *status);
@property (nonatomic, copy, nullable) void (^popupPreviewChanged)(NSString *preview);
@property (nonatomic, copy, nullable) void (^popupFrozenChanged)(BOOL frozen);
@property (nonatomic, copy, nullable) void (^popupRefiningChanged)(BOOL refining);
// The last words of the refinement stream, emitted only while refining.
@property (nonatomic, copy, nullable) void (^popupRefinementPreviewChanged)(NSString *preview);
@property (nonatomic, copy, nullable) void (^popupOAuthRefreshRequested)(void);
@property (nonatomic, copy, nullable) void (^popupListeningIndicatorRequested)(void);
@property (nonatomic, copy, nullable) void (^popupErrorRequested)(NSString *message);
// The panel is on screen, so the session need not wait out its fallback timer
// before opening the microphone.
- (void)notePopupPresented:(uint64_t)generation NS_SWIFT_NAME(notePopupPresented(generation:));

// The last transcript Speecher heard, which the menu bar panel offers to copy
// again. Empty until one exists.
@property (nonatomic, readonly, copy) NSString *lastTranscript;
@property (nonatomic, copy, nullable) void (^transcriptChanged)(NSString *transcript);

// The desktop-wide shortcut, which nothing surfaced after the setup assistant.
@property (nonatomic, readonly) BOOL shortcutSupported;
// The bound sequence as macOS writes it, such as ⌃⌥D. Empty while none is.
@property (nonatomic, readonly, copy) NSString *shortcutDisplay;
// The characters the key types with no modifiers held, plus NSEvent's modifier
// flags. nil once bound, otherwise why the binding was refused.
- (nullable NSString *)bindShortcutWithCharacters:(NSString *)characters
                                    modifierFlags:(NSUInteger)modifierFlags
    NS_SWIFT_NAME(bindShortcut(characters:modifierFlags:));
// Registers the sequence the binder already reports, which is its built-in
// default before anything was ever stored. nil once bound, otherwise why not.
- (nullable NSString *)bindCurrentShortcut;
// A registered hotkey is consumed system-wide and never reaches a recorder's
// key monitor: pressing the bound combination while recording would start
// dictation instead of re-recording it. Recording therefore lets go of the
// registration first; ending it restores the bound sequence.
- (void)beginShortcutRecording;
- (nullable NSString *)endShortcutRecording;

// The single-key half of the binding: one physical key on its own, which
// Speecher watches itself rather than registers as a hotkey. The recorder
// reaches it by macOS virtual keycode; the stable identity across the core is
// the W3C KeyboardEvent.code name.
// The code name of the bound single key, or nil while a combination holds
// the binding.
@property (nonatomic, readonly, copy, nullable) NSString *currentSingleKeyCode;
// The code name for a macOS virtual keycode, or nil for a key outside the
// vocabulary (a media key), which a recorder ignores.
- (nullable NSString *)keyCodeNameForMacKeyCode:(unsigned short)keyCode
    NS_SWIFT_NAME(keyCodeName(forMacKeyCode:));
// What the UI calls the key, such as "Right Option".
- (NSString *)displayForSingleKeyCode:(NSString *)code
    NS_SWIFT_NAME(display(forSingleKeyCode:));
// nil when the backend can honour the key; otherwise why not, the missing
// Accessibility grant being the common case. Asking does not save.
- (nullable NSString *)unsupportedReasonForSingleKeyCode:(NSString *)code
    NS_SWIFT_NAME(unsupportedReason(forSingleKeyCode:));
// Binds and stores the key. nil once bound, otherwise why it was refused.
- (nullable NSString *)bindSingleKeyCode:(NSString *)code
    NS_SWIFT_NAME(bindSingleKey(code:));
// The non-blocking caveat recording this key earns: a key that types keeps
// typing, and Option keys are how macOS composes accents. Empty for a key
// that carries no text. The binding saves regardless.
- (NSString *)warningForSingleKeyCode:(NSString *)code
    NS_SWIFT_NAME(warning(forSingleKeyCode:));

@property (nonatomic, readonly) BOOL accessibilitySupported;
@property (nonatomic, readonly) BOOL accessibilityEnabled;
// nil once the grant was asked for; otherwise why it could not be.
- (nullable NSString *)enableAccessibility;

// The setup assistant's seams into the core, which the Qt assistant reached
// through C++ and the SwiftUI one reaches here.

// Every provider the registry offers, in the order it offers them.
@property (nonatomic, readonly, copy) NSArray<SpeecherProviderModel *> *speechProviders;
@property (nonatomic, readonly, copy) NSArray<SpeecherProviderModel *> *refinementProviders;
// Ordered label/value pairs from the registry descriptor, which the assistant
// shows under the provider picker. Empty for an unknown id.
- (NSArray<NSArray<NSString *> *> *)statsForSpeechProvider:(NSString *)providerId;
- (NSArray<NSArray<NSString *> *> *)statsForRefinementProvider:(NSString *)providerId;
// Prepares every provider of the kind, off the main thread where the provider
// offers that, and reports each verdict on the main thread as it lands.
// `message` says why a provider is not ready and is empty when it is. A newer
// round supersedes an older one of the same kind, whose replies are dropped.
- (void)checkSpeechProviders:(void (^)(NSString *providerId, BOOL ready, NSString *message))report;
// One provider only, for a sign-in change: the other rows' verdicts stay
// live, and no extra OAuth refreshes run — matching the Qt and WinUI
// assistants.
- (void)checkSpeechProviderNamed:(NSString *)providerId
                          report:(void (^)(NSString *providerId, BOOL ready, NSString *message))report
    NS_SWIFT_NAME(checkSpeechProvider(named:report:));
- (void)checkRefinementProviders:(void (^)(NSString *providerId, BOOL ready, NSString *message))report;
// Whether the own-model refinement choice (Local or Custom endpoint) has what
// it needs, read from the settings now: its verdict follows every edit rather
// than a probe.
@property (nonatomic, readonly) BOOL ownModelRefinementReady;
// Core's setup policy: the provider auto-selection may move to, and which
// providers are sign-ins.
- (NSString *)setupProviderChoiceForSaved:(NSString *)saved
                           readyProviders:(NSArray<NSString *> *)readyProviders
                         explicitlyChosen:(BOOL)explicitlyChosen
    NS_SWIFT_NAME(setupProviderChoice(saved:readyProviders:explicitlyChosen:));
- (BOOL)isSetupSignInProvider:(NSString *)providerId NS_SWIFT_NAME(isSetupSignInProvider(_:));
// "Ollama with gemma4:e4b" or "qwen3 on your server"; empty for other providers.
@property (nonatomic, readonly, copy) NSString *ownModelRefinementSummary;

// The CLI Proxy API sign-in a service can be opted into during setup. The
// decisions live in core (ProviderSignIn); these are its seams, so this
// assistant behaves exactly like the Qt and WinUI ones.
// Whether any enabled CLI Proxy API account exists for a registered speech
// provider: the Welcome step's reason to open its gate.
@property (nonatomic, readonly) BOOL setupCliproxyAccountsAvailable;
- (BOOL)setupSupportsCliproxyForProvider:(NSString *)providerId
    NS_SWIFT_NAME(setupSupportsCliproxy(provider:));
- (BOOL)setupUsesCliproxyForProvider:(NSString *)providerId
    NS_SWIFT_NAME(setupUsesCliproxy(provider:));
// Opting out returns to the sign-in mode the assistant first saw, so a mode
// chosen in Settings survives a round trip through CLI Proxy API.
- (void)setSetupUseCliproxy:(BOOL)use forProvider:(NSString *)providerId
    NS_SWIFT_NAME(setSetupUseCliproxy(_:provider:));
- (NSArray<RowOptionModel *> *)setupCliproxyAccountOptionsForProvider:(NSString *)providerId
    NS_SWIFT_NAME(setupCliproxyAccountOptions(provider:));
- (NSString *)setupCliproxyAccountForProvider:(NSString *)providerId
    NS_SWIFT_NAME(setupCliproxyAccount(provider:));
- (void)setSetupCliproxyAccount:(NSString *)account forProvider:(NSString *)providerId
    NS_SWIFT_NAME(setSetupCliproxyAccount(_:provider:));
// The shared CLI Proxy API copy (ProviderSignIn): the opt-in label and the
// Welcome step's two hints, one of which quotes that label.
@property (nonatomic, readonly, copy) NSString *setupCliproxyOptInLabel;
@property (nonatomic, readonly, copy) NSString *setupCliproxyFoundHint;
@property (nonatomic, readonly, copy) NSString *setupCliproxyMissingHint;
// The configured account directory; empty means automatic detection.
@property (nonatomic, readonly, copy) NSString *setupCliproxyDirectory;
// Where detection currently lands, for the directory field's placeholder.
@property (nonatomic, readonly, copy) NSString *setupCliproxyDirectoryPlaceholder;
- (void)setSetupCliproxyDirectory:(NSString *)directory
    NS_SWIFT_NAME(setSetupCliproxyDirectory(_:));

// A live microphone meter over the input device the settings name. Levels and
// failures arrive on the main thread until the meter is stopped.
- (void)startMicrophoneMeterOnLevel:(void (^)(float level))onLevel
                            failure:(void (^)(NSString *message))onFailure
    NS_SWIFT_NAME(startMicrophoneMeter(onLevel:failure:));
- (void)stopMicrophoneMeter;
// The system input volume of the default microphone, 0 to 1, or -1 when macOS
// does not say.
- (float)microphoneInputVolume;

// Re-reads the Accessibility grant. macOS records it against the app signature
// and never tells the process that asked, so noticing it means looking again;
// a change arrives through accessibilityChanged.
- (void)refreshAccessibilityState;
// Shows the system Accessibility prompt and opens System Settings on it. nil
// unless macOS refused in a way worth repeating to the person.
- (nullable NSString *)requestAccessibilityGrant;

// Marks setup complete, which is what stops the assistant appearing at launch.
- (void)completeSetup;
// Quits and starts this build again. The Accessibility grant only reaches a
// process started after it was given.
- (void)relaunch;

// The OpenAI credential as the Providers page shows it: a status line for every
// credential source except the app settings key, which is a secret this front
// end reads from and writes to the keyring itself.
@property (nonatomic, readonly) BOOL credentialIsEditable;
@property (nonatomic, readonly, copy) NSString *credentialStatus;
@property (nonatomic, readonly, copy) NSString *anthropicCredentialStatus;
@property (nonatomic, copy, nullable) void (^anthropicCredentialsChanged)(void);
- (NSString *)readApiKey;
// nil when the keyring took it, otherwise why it refused.
- (nullable NSString *)saveApiKey:(NSString *)apiKey;

// Running models on this computer: ApplicationController's LocalSetup, so a
// download, a Speed Test or a pull outlives the window that started it.
// Whether this build can run speech on this computer at all.
@property (nonatomic, readonly) BOOL localSpeechAvailable;
// Anything below changed. rowsChanged says the settings rows that report it,
// and the settings LocalSetup writes, changed too; it is NO for a download or
// pull's progress alone. Arrives on the main thread, often while one runs.
@property (nonatomic, copy, nullable) void (^localSetupChanged)(BOOL rowsChanged);
@property (nonatomic, readonly, strong) LocalSetupState *localSetupState;
// Starts the hardware probe the first time.
- (void)probeLocalHardware;
// Saves the suggestion as the choice unless a model was already chosen.
- (void)initializeSpeechModel;
// Saves an explicit choice, whichever provider is selected.
- (void)chooseSpeechModel:(NSString *)modelId NS_SWIFT_NAME(chooseSpeechModel(_:));
- (void)downloadLocalModel:(NSString *)modelId NS_SWIFT_NAME(downloadLocalModel(_:));
- (void)cancelLocalModelDownload:(NSString *)modelId NS_SWIFT_NAME(cancelLocalModelDownload(_:));
- (void)deleteLocalModel:(NSString *)modelId NS_SWIFT_NAME(deleteLocalModel(_:));
- (void)testLocalModelSpeed:(NSString *)modelId NS_SWIFT_NAME(testLocalModelSpeed(_:));

- (void)detectLocalRunners;
- (void)pullCleanupModel:(NSString *)ollamaTag NS_SWIFT_NAME(pullCleanupModel(_:));

@property (nonatomic, readonly, strong) RefinementEndpointForm *refinementEndpointForm;
// Saves the fields the person edited; nil is a field left alone. Core decides
// what an edit does to a preset and a saved key.
- (void)editRefinementEndpointFormat:(nullable NSString *)format
                           serverUrl:(nullable NSString *)serverUrl
                              apiKey:(nullable NSString *)apiKey
                               model:(nullable NSString *)model
    NS_SWIFT_NAME(editRefinementEndpoint(format:serverUrl:apiKey:model:));
// Tests the saved endpoint; the verdict and model list arrive with
// localSetupChanged.
- (void)checkRefinementEndpoint;
@end

#ifdef __cplusplus
namespace speecher {
class ApplicationController;
}

@interface SpeecherBridge (Cxx)
- (instancetype)initWithController:(speecher::ApplicationController *)controller;
@end
#endif

NS_ASSUME_NONNULL_END
