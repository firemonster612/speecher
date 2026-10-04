#pragma once

#import <AppKit/AppKit.h>
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
    // Any number of the options, held as an array of their ids; none means all.
    SpeecherColumnKindChoiceSet,
    // An icon per record that says something about it; it holds no value of
    // the record's and is never a field in the sheet.
    SpeecherColumnKindIcon,
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
// Choice and ChoiceSet columns only.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *options;
// The column that takes the leftover width; the others size to content.
@property (nonatomic, readonly) BOOL stretch;
// Text columns only: the value may hold several lines.
@property (nonatomic, readonly) BOOL multiline;
// Text columns only: an example shown in an empty field of the sheet.
@property (nonatomic, readonly, copy) NSString *placeholder;
// A field of the sheet that adds or edits a record, but not a column of the table.
@property (nonatomic, readonly) BOOL dialogOnly;
// Said under the field in the sheet.
@property (nonatomic, readonly, copy) NSString *help;
// Text columns only: the column whose value the cell shows as a muted second line.
@property (nonatomic, readonly, copy) NSString *detailColumn;
// ChoiceSet columns only: the sheet's two choices, every option or the ticked ones.
@property (nonatomic, readonly, copy) NSString *everyChoice;
@property (nonatomic, readonly, copy) NSString *someChoice;
// Icon columns only: the schema's platform-neutral icon ("microphone", "star").
@property (nonatomic, readonly, copy) NSString *iconId;
@end

// What an Icon column shows for one record (speecher::IconCell).
typedef NS_ENUM(NSInteger, SpeecherIconState) {
    SpeecherIconStateNone,
    SpeecherIconStateShown,
    // The icon in the secondary style.
    SpeecherIconStateFaint,
};

@interface CollectionIconCell : NSObject
@property (nonatomic, readonly) SpeecherIconState state;
@property (nonatomic, readonly, copy) NSString *tooltip;
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
@property (nonatomic, readonly, copy) NSString *addDialogTitle;
// Opens the selected record in the add sheet, titled after it; empty where
// records are only edited in place.
@property (nonatomic, readonly, copy) NSString *editLabel;
// Deletes the selection with no confirmation; Undo delete puts it back.
@property (nonatomic, readonly, copy) NSString *deleteLabel;
// What an empty editor says; empty on a collection that always has records.
@property (nonatomic, readonly, copy) NSString *emptyTitle;
@property (nonatomic, readonly, copy) NSString *emptyHelp;
// Empty unless the collection can also be filled from a file.
@property (nonatomic, readonly, copy) NSString *importLabel;
// What a refused import is titled.
@property (nonatomic, readonly, copy) NSString *importFailureTitle;
@property (nonatomic, readonly, copy) NSArray<NSString *> *importFileExtensions;
// Commands beyond add and delete, which the editor implements itself.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *actions;
@property (nonatomic, readonly) NSInteger minimumHeight;
@end

// Which provider chain a fallback list belongs to (speecher::ProviderRole).
typedef NS_ENUM(NSInteger, SpeecherProviderRole) {
    SpeecherProviderRoleSpeech,
    SpeecherProviderRoleRefinement,
};

// One fallback as its row shows it (speecher::FallbackItem).
@interface SpeecherFallbackItem : NSObject
@property (nonatomic, readonly, copy) NSString *providerId;
@property (nonatomic, readonly, copy) NSString *label;
// Which turn it gets, or why it can't stand in right now.
@property (nonatomic, readonly, copy) NSString *status;
// The status reads in the negative role (speecher::StatusTone::Negative).
@property (nonatomic, readonly) BOOL negative;
@property (nonatomic, readonly) BOOL canMoveUp;
@property (nonatomic, readonly) BOOL canMoveDown;
@end

// A role's ordered fallbacks as the Fallbacks subpage and the setup assistant
// show them (speecher::FallbackListPresentation).
@interface SpeecherFallbackList : NSObject
@property (nonatomic, readonly) SpeecherProviderRole role;
// Above the card, with the subtitle under it; the footer goes under the card.
@property (nonatomic, readonly, copy) NSString *heading;
@property (nonatomic, readonly, copy) NSString *subtitle;
@property (nonatomic, readonly, copy) NSString *footer;
@property (nonatomic, readonly, copy) NSArray<SpeecherFallbackItem *> *items;
// The Add row, shown only while canAdd; the placeholder chooses nothing.
@property (nonatomic, readonly) BOOL canAdd;
@property (nonatomic, readonly, copy) NSString *addLabel;
@property (nonatomic, readonly, copy) NSString *addHelp;
@property (nonatomic, readonly, copy) NSString *addPlaceholder;
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *addChoices;
// The row buttons' captions, which are also their accessible names.
@property (nonatomic, readonly, copy) NSString *moveUpCaption;
@property (nonatomic, readonly, copy) NSString *moveDownCaption;
@property (nonatomic, readonly, copy) NSString *removeCaption;
@end

// The optional fallbacks section of the Transcription and Refinement setup
// steps (speecher::SetupFallbackPresentation). Hidden while visible is NO.
@interface SpeecherSetupFallbackSection : NSObject
@property (nonatomic, readonly) BOOL visible;
@property (nonatomic, readonly, copy) NSString *hint;
// Empty when there is nothing to suggest.
@property (nonatomic, readonly, copy) NSString *suggestion;
@property (nonatomic, readonly, copy) NSString *suggestionAction;
@property (nonatomic, readonly, strong) SpeecherFallbackList *list;
@end

@interface SettingsRowModel : NSObject
@property (nonatomic, readonly, copy) NSString *rowId;
// As the draft words it: a status row titled "API key" in key mode.
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
// The row offers values, even while the list is empty (a server not yet
// asked), so it keeps the same control.
@property (nonatomic, readonly) BOOL suggests;
// A Collection row that is not enabled stays readable: only adding, editing
// and deleting stop.
@property (nonatomic, readonly) BOOL enabled;
// Shown on the control, and replaced by disabledHelp while enabled says no.
@property (nonatomic, readonly, copy) NSString *tooltip;
// Names the gate that is closed, where a row has more than one.
@property (nonatomic, readonly, copy) NSString *disabledHelp;
// The action that can lift the gate while enabled says no — an action id the
// row dispatch understands — and its button caption. Empty when none.
@property (nonatomic, readonly, copy) NSString *disabledAction;
@property (nonatomic, readonly, copy) NSString *disabledActionLabel;
// Rows naming the same group share one gate, so one note explains it.
@property (nonatomic, readonly, copy) NSString *groupId;
// Set on a Collection row, and on the one Custom row that is a table.
@property (nonatomic, readonly, strong, nullable) CollectionModel *collection;
// Text rows only: a key or password, shown masked.
@property (nonatomic, readonly) BOOL secret;
// Text rows, and Custom rows that take text: shown in the empty field.
@property (nonatomic, readonly, copy) NSString *placeholder;
// Text rows only: the value may hold several lines.
@property (nonatomic, readonly) BOOL multiline;
// The help says the row's choice can't work right now, and reads in the
// negative role (speecher::SettingsRow::helpTone).
@property (nonatomic, readonly) BOOL helpNegative;
// Action rows only: the subpage the whole row opens, or empty.
@property (nonatomic, readonly, copy) NSString *targetPage;
// The fallback list rows only, as the draft and the live facts present them.
@property (nonatomic, readonly, strong, nullable) SpeecherFallbackList *fallbackList;
@end

@interface SettingsSectionModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly, copy) NSArray<SettingsRowModel *> *rows;
@end

@interface SettingsPageModel : NSObject
@property (nonatomic, readonly, copy) NSString *pageId;
@property (nonatomic, readonly, copy) NSArray<SettingsSectionModel *> *sections;
@end

// What a pane's groups are to each other. Mirrors speecher::PaneLayout.
typedef NS_ENUM(NSInteger, SpeecherPaneLayout) {
    SpeecherPaneLayoutSections,
    SpeecherPaneLayoutAlternatives,
    SpeecherPaneLayoutTranscribe,
    SpeecherPaneLayoutHome,
};

// One card a pane shows: a heading, a footnote, and the schema rows it names,
// as this build has them. An Alternatives pane addresses it as "pane:view".
@interface SettingsPaneGroupModel : NSObject
@property (nonatomic, readonly, copy) NSString *view;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *help;
@property (nonatomic, readonly, copy) NSArray<NSString *> *rows;
@end

// One sidebar entry of the settings window, as the schema arranges them.
@interface SettingsPaneModel : NSObject
@property (nonatomic, readonly, copy) NSString *paneId;
@property (nonatomic, readonly, copy) NSString *title;
// Platform-neutral; PaneIcons.swift maps it to an SF Symbol.
@property (nonatomic, readonly, copy) NSString *iconId;
@property (nonatomic, readonly) SpeecherPaneLayout layout;
@property (nonatomic, readonly, copy) NSArray<SettingsPaneGroupModel *> *groups;
@end

// A page one step below a pane, with no sidebar entry of its own
// (speecher::SettingsSubpage).
@interface SettingsSubpageModel : NSObject
// "dictation:fallbacks", as resolvePage takes it.
@property (nonatomic, readonly, copy) NSString *subpageId;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSArray<SettingsPaneGroupModel *> *groups;
@end

// One pane a settings search finds, with the visible rows on it whose label or
// help mention the query, in reading order (speecher::SearchMatch). No rows
// means the pane matched by its title or a group's.
@interface SettingsSearchMatch : NSObject
@property (nonatomic, readonly, copy) NSString *pane;
@property (nonatomic, readonly, copy) NSArray<NSString *> *rows;
@end

// One titled group of the sidebar; the top group's title is empty.
@interface SidebarGroupModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSArray<NSString *> *panes;
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
// The sidebar's panes and their groups, which never change while the app runs.
@property (nonatomic, readonly, copy) NSArray<SettingsPaneModel *> *panes;
@property (nonatomic, readonly, copy) NSArray<SidebarGroupModel *> *sidebarGroups;
@property (nonatomic, readonly, copy) NSArray<SettingsSubpageModel *> *subpages;
// A page id ("general", "vocabulary:corrections", "dictation:fallbacks") as
// the pane, view and subpage it names, speecher::resolvePage's answer: an
// unknown pane, or a view the pane does not have, gives Home.
- (NSArray<NSString *> *)resolvePage:(NSString *)pageId NS_SWIFT_NAME(resolvePage(_:));
// One edit of a role's fallbacks (speecher::withFallbackMoved, Removed and
// Added), written to the draft as the list row's value. Commit saves it.
- (void)moveFallback:(SpeecherProviderRole)role at:(NSInteger)index by:(NSInteger)offset
    NS_SWIFT_NAME(moveFallback(_:at:by:));
- (void)removeFallback:(SpeecherProviderRole)role at:(NSInteger)index NS_SWIFT_NAME(removeFallback(_:at:));
- (void)addFallback:(SpeecherProviderRole)role provider:(NSString *)providerId
    NS_SWIFT_NAME(addFallback(_:provider:));
// The panes a sidebar search shows, from the core index, with rows as the
// draft shows them.
- (NSArray<SettingsSearchMatch *> *)searchSettings:(NSString *)query NS_SWIFT_NAME(searchSettings(_:));
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
// Lists the microphones again, for the device row and its gate. Once loaded,
// a snapshot reuses the last list rather than enumerating devices itself.
- (void)refreshAudioInputs;
// Empty when these records are consistent; otherwise one message per problem.
- (NSArray<NSString *> *)problemsWith:(NSArray<SpeecherRecord *> *)records forRowId:(NSString *)rowId;
// What deleting the custom Writing Profile `profileId` changes in the draft,
// empty when nothing points at it.
- (NSString *)writingProfileDeletionNotice:(NSString *)profileId
    NS_SWIFT_NAME(writingProfileDeletionNotice(_:));
// The title of the confirmation that notice goes in.
@property (nonatomic, readonly, copy) NSString *writingProfileDeletionTitle;
// Save against the records this editor last submitted, not a later model refresh.
- (NSArray<NSString *> *)saveRecords:(NSArray<SpeecherRecord *> *)records
                    previousRecords:(NSArray<SpeecherRecord *> *)previous
                           forRowId:(NSString *)rowId
    NS_SWIFT_NAME(save(records:previousRecords:forRowId:));
- (CollectionImportResult *)recordsImportedFrom:(NSData *)data
                                           into:(NSArray<SpeecherRecord *> *)records
                                       forRowId:(NSString *)rowId;
// An Icon column's cell for each of these records, as they stand with the
// draft's other settings.
- (NSArray<CollectionIconCell *> *)iconsForColumn:(NSString *)columnId
                                          inRowId:(NSString *)rowId
                                          records:(NSArray<SpeecherRecord *> *)records;
// What a ChoiceSet cell says for these option ids, speecher::choiceSetText.
- (NSString *)choiceSetTextForColumn:(NSString *)columnId
                             inRowId:(NSString *)rowId
                                 ids:(NSArray<NSString *> *)ids;
// What a cell says on hover, which a learned correction answers per record.
- (NSString *)tooltipForColumn:(NSString *)columnId
                      inRowId:(NSString *)rowId
                       record:(SpeecherRecord *)record;
@end

// speecher::UpdateBannerModel: what every update banner shows. Empty captions
// mean the button is not offered.
typedef NS_ENUM(NSInteger, SpeecherBannerTone) {
    SpeecherBannerToneInformation,
    SpeecherBannerTonePositive,
    SpeecherBannerToneError,
};

@interface SpeecherUpdateBanner : NSObject
@property (nonatomic, readonly) BOOL visible;
@property (nonatomic, readonly) BOOL showInPopup;
@property (nonatomic, readonly) SpeecherBannerTone tone;
@property (nonatomic, readonly, copy) NSString *text;
// 0 to 100 while downloading, otherwise -1.
@property (nonatomic, readonly) NSInteger progress;
@property (nonatomic, readonly, copy) NSString *action;
@property (nonatomic, readonly) BOOL actionEnabled;
@property (nonatomic, readonly, copy) NSString *later;
@property (nonatomic, readonly, copy) NSString *dismiss;
@end

// The update states the preview renderer captures, seeded with sample facts
// and worded by core.
typedef NS_ENUM(NSInteger, SpeecherUpdatePreviewState) {
    SpeecherUpdatePreviewStateAvailable,
    SpeecherUpdatePreviewStateDownloading,
    SpeecherUpdatePreviewStateReadyToRestart,
    SpeecherUpdatePreviewStateError,
    SpeecherUpdatePreviewStateManualInstall,
    SpeecherUpdatePreviewStateCheckFailed,
};

@interface SpeecherUpdateBanner (Preview)
+ (SpeecherUpdateBanner *)previewForState:(SpeecherUpdatePreviewState)state;
@end

// speecher::WhatsNewBannerModel.
@interface SpeecherWhatsNewBanner : NSObject
@property (nonatomic, readonly, copy) NSString *text;
@property (nonatomic, readonly, copy) NSString *action;
@property (nonatomic, readonly, copy) NSString *dismiss;
+ (SpeecherWhatsNewBanner *)previewForVersion:(NSString *)version;
@end

// speecher::ErrorFix: what a dictation error offers to fix it.
typedef NS_ENUM(NSInteger, SpeecherErrorFix) {
    SpeecherErrorFixNone,
    SpeecherErrorFixSettingsPage,
    SpeecherErrorFixMicrophonePermission,
    SpeecherErrorFixAccessibilityPermission,
};

// speecher::PopupErrorAction, with the caption of the button that offers it.
@interface SpeecherErrorAction : NSObject
@property (nonatomic, readonly) SpeecherErrorFix fix;
// The settings page to open, for SpeecherErrorFixSettingsPage.
@property (nonatomic, readonly, copy) NSString *pageId;
// "Open Accounts"; empty for SpeecherErrorFixNone.
@property (nonatomic, readonly, copy) NSString *label;
- (instancetype)initWithFix:(SpeecherErrorFix)fix pageId:(NSString *)pageId NS_SWIFT_NAME(init(fix:pageId:));
@end

// A sign-in's status line, and whether it says the sign-in works.
@interface SpeecherCredentialStatus : NSObject
@property (nonatomic, readonly, copy) NSString *text;
@property (nonatomic, readonly) BOOL ready;
@end

// speecher::SessionControls: the popup's buttons for a session state.
@interface SpeecherSessionControls : NSObject
@property (nonatomic, readonly) BOOL pauseVisible;
@property (nonatomic, readonly) BOOL pauseEnabled;
@property (nonatomic, readonly) BOOL paused;
@property (nonatomic, readonly) BOOL cancelVisible;
// A spinner in pause's place while transcribing and refining.
@property (nonatomic, readonly) BOOL busyVisible;
@end

// speecher::PreviewLine: the preview as its line shows it, and whether words
// were cut from its front.
@interface SpeecherPreviewLine : NSObject
@property (nonatomic, readonly, copy) NSString *text;
@property (nonatomic, readonly) BOOL cut;
@end

// The dictation popup's measurements (speecher::popup), shared with the Linux
// and Windows popups. Points.
@interface SpeecherPopupGeometry : NSObject
@property (class, nonatomic, readonly) CGFloat pillHeight;
@property (class, nonatomic, readonly) CGFloat pillMinimumWidth;
@property (class, nonatomic, readonly) CGFloat buttonSize;
@property (class, nonatomic, readonly) CGFloat buttonIconSize;
@property (class, nonatomic, readonly) CGFloat buttonGap;
@property (class, nonatomic, readonly) CGFloat lobeAir;
@property (class, nonatomic, readonly) NSInteger barCount;
@property (class, nonatomic, readonly) CGFloat barWidth;
@property (class, nonatomic, readonly) CGFloat barGap;
@property (class, nonatomic, readonly) CGFloat barDotHeight;
@property (class, nonatomic, readonly) CGFloat compactStripHeight;
@property (class, nonatomic, readonly) CGFloat previewTopMargin;
@property (class, nonatomic, readonly) CGFloat previewSideMargin;
@property (class, nonatomic, readonly) CGFloat previewBottomMargin;
@property (class, nonatomic, readonly) CGFloat previewStripSpacing;
@property (class, nonatomic, readonly) CGFloat shoulderDrop;
@property (class, nonatomic, readonly) CGFloat fillet;
@property (class, nonatomic, readonly) CGFloat lobeRadius;
@property (class, nonatomic, readonly) CGFloat maxPreviewWidth;
@property (class, nonatomic, readonly) CGFloat previewFontScale;
@property (class, nonatomic, readonly) CGFloat previewFadeWidth;
+ (CGFloat)minimumPreviewBarWidthForLobeWidth:(CGFloat)lobeWidth shoulderHeight:(CGFloat)shoulderHeight
    NS_SWIFT_NAME(minimumPreviewBarWidth(lobeWidth:shoulderHeight:));
@end

// speecher::PopupOutcome: how a dictation ended, which picks the receipt's symbol.
typedef NS_ENUM(NSInteger, SpeecherPopupOutcome) {
    SpeecherPopupOutcomeInserted,
    SpeecherPopupOutcomeCopied,
    SpeecherPopupOutcomeFallback,
    SpeecherPopupOutcomeError,
    SpeecherPopupOutcomeCancelled,
};

// One step of the setup assistant (speecher::SetupStepInfo).
@interface SpeecherSetupStep : NSObject
@property (nonatomic, readonly, copy) NSString *stepId;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *intro;
@property (nonatomic, readonly, copy) NSString *blocked;
@end

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
// One or two lines on what the provider is good at, for a picker's help.
@property (nonatomic, readonly, copy) NSString *summary;
@end

// Where a batch saves its transcripts. Mirrors speecher::TranscriptDestination.
typedef NS_ENUM(NSInteger, SpeecherTranscriptDestination) {
    SpeecherTranscriptDestinationBesideInput,
    SpeecherTranscriptDestinationFolder,
    // Saved nowhere: the transcripts are only shown.
    SpeecherTranscriptDestinationNowhere,
};

// One batch's choices. Mirrors speecher::TranscribeOptions; ids are the ones
// the provider and cleanup/tone/profile lists below carry.
@interface SpeecherTranscribeOptions : NSObject
@property (nonatomic, copy) NSString *speechProviderId;
@property (nonatomic) BOOL applyVocabulary;
// "none" skips refinement.
@property (nonatomic, copy) NSString *refinementProviderId;
@property (nonatomic, copy) NSString *cleanupStrength;
@property (nonatomic, copy) NSString *tone;
@property (nonatomic, copy) NSString *writingProfile;
@property (nonatomic) SpeecherTranscriptDestination destination;
// Used only with SpeecherTranscriptDestinationFolder.
@property (nonatomic, copy) NSString *folder;
@end

// One file's outcome. Mirrors speecher::TranscribeFileResult.
@interface SpeecherTranscriptResult : NSObject
@property (nonatomic, readonly, copy) NSString *path;
// What the speech provider heard.
@property (nonatomic, readonly, copy) NSString *raw;
// The saved transcript: refined when refinement ran, otherwise the raw text
// after vocabulary corrections. Empty when the file failed.
@property (nonatomic, readonly, copy) NSString *refined;
// Empty unless the transcript was written to disk.
@property (nonatomic, readonly, copy) NSString *savedPath;
// Why the file failed, or what went wrong on the way for one that finished.
@property (nonatomic, readonly, copy) NSString *error;
@property (nonatomic, readonly) BOOL failed;
@end

// What Export writes. Mirrors speecher::TranscriptFormat.
typedef NS_ENUM(NSInteger, SpeecherTranscriptFormat) {
    SpeecherTranscriptFormatText,
    SpeecherTranscriptFormatSrt,
    SpeecherTranscriptFormatWebVtt,
};

// Mirrors speecher::TranscribePhase.
typedef NS_ENUM(NSInteger, SpeecherTranscribePhase) {
    SpeecherTranscribePhaseReading,
    SpeecherTranscribePhaseTranscribing,
    SpeecherTranscribePhaseFinishing,
    SpeecherTranscribePhaseRefining,
};

// Mirrors speecher::TranscribeStep.
typedef NS_ENUM(NSInteger, SpeecherTranscribeStep) {
    SpeecherTranscribeStepConfigure,
    SpeecherTranscribeStepTranscribe,
    SpeecherTranscribeStepExport,
};

// Mirrors speecher::TranscribeText.
typedef NS_ENUM(NSInteger, SpeecherTranscribeText) {
    SpeecherTranscribeTextWindowTitle,
    SpeecherTranscribeTextAudioFilesSection,
    SpeecherTranscribeTextTranscriptionSection,
    SpeecherTranscribeTextRefinementSection,
    SpeecherTranscribeTextOutputSection,
    SpeecherTranscribeTextFilesDialogTitle,
    SpeecherTranscribeTextRemoveFile,
    SpeecherTranscribeTextService,
    SpeecherTranscribeTextVocabulary,
    SpeecherTranscribeTextVocabularyHelp,
    SpeecherTranscribeTextRefiner,
    SpeecherTranscribeTextRefinerHelp,
    SpeecherTranscribeTextNoRefiner,
    SpeecherTranscribeTextRefinerModel,
    SpeecherTranscribeTextCleanup,
    SpeecherTranscribeTextCleanupHelp,
    SpeecherTranscribeTextWritingProfile,
    SpeecherTranscribeTextWritingProfileHelp,
    SpeecherTranscribeTextTone,
    SpeecherTranscribeTextToneHelp,
    SpeecherTranscribeTextSaveTranscripts,
    SpeecherTranscribeTextFolder,
    SpeecherTranscribeTextChangeFolder,
    SpeecherTranscribeTextFolderDialogTitle,
    SpeecherTranscribeTextCancel,
    SpeecherTranscribeTextPartialName,
    SpeecherTranscribeTextPartialPlaceholder,
    SpeecherTranscribeTextRefined,
    SpeecherTranscribeTextRaw,
    SpeecherTranscribeTextCopyAll,
    SpeecherTranscribeTextCopy,
    SpeecherTranscribeTextCopied,
    SpeecherTranscribeTextExportAll,
    SpeecherTranscribeTextExport,
    SpeecherTranscribeTextExportAllDialogTitle,
    SpeecherTranscribeTextExportDialogTitle,
    SpeecherTranscribeTextTextFiles,
    SpeecherTranscribeTextSaved,
    SpeecherTranscribeTextRetry,
    SpeecherTranscribeTextRetrying,
    SpeecherTranscribeTextTranscribeMore,
    SpeecherTranscribeTextProgressName,
    SpeecherTranscribeTextNoFilesYet,
    SpeecherTranscribeTextNeedsRefiner,
    SpeecherTranscribeTextDropToAdd,
};

// Mirrors speecher::DictationState, so the panel decides on the state rather
// than on the words of its label.
typedef NS_ENUM(NSInteger, SpeecherDictationState) {
    SpeecherDictationStateIdle,
    SpeecherDictationStateStarting,
    SpeecherDictationStateListening,
    SpeecherDictationStatePaused,
    SpeecherDictationStateStopping,
    SpeecherDictationStateRefining,
    SpeecherDictationStateDelivering,
    SpeecherDictationStateError,
};

// Mirrors speecher::MicrophoneTestState.
typedef NS_ENUM(NSInteger, SpeecherMicrophoneTestState) {
    SpeecherMicrophoneTestStateStopped,
    SpeecherMicrophoneTestStateStarting,
    SpeecherMicrophoneTestStateRunning,
};

// Mirrors speecher::LocalModelText.
typedef NS_ENUM(NSInteger, SpeecherLocalModelText) {
    SpeecherLocalModelTextUseModel,
    SpeecherLocalModelTextTestSpeed,
    SpeecherLocalModelTextInUse,
    SpeecherLocalModelTextSuggested,
    SpeecherLocalModelTextYourChoice,
    SpeecherLocalModelTextTooLarge,
    SpeecherLocalModelTextHideOtherModels,
    SpeecherLocalModelTextCompareNote,
    SpeecherLocalModelTextDeleteModel,
    SpeecherLocalModelTextDeleteBody,
};

// Mirrors speecher::SetupText.
typedef NS_ENUM(NSInteger, SpeecherSetupText) {
    SpeecherSetupTextSkipSetup,
    SpeecherSetupTextCheckAgain,
    SpeecherSetupTextGoToStep,
    SpeecherSetupTextHowToDictate,
    SpeecherSetupTextTranscriptionService,
    SpeecherSetupTextLocalSpeechNote,
    SpeecherSetupTextDownloadToContinue,
    SpeecherSetupTextDownloadContinues,
    SpeecherSetupTextCloseWhileDownloading,
    SpeecherSetupTextCliproxyAccount,
    SpeecherSetupTextListeningForInput,
    SpeecherSetupTextInputDetected,
    SpeecherSetupTextUsesYourSignIn,
    SpeecherSetupTextYourOwnModels,
    SpeecherSetupTextCleanupProvider,
    SpeecherSetupTextSkipCleanup,
    SpeecherSetupTextLookingForRunners,
    SpeecherSetupTextNoRunnerFound,
    SpeecherSetupTextNoRunner,
    SpeecherSetupTextRawUntilRunner,
    SpeecherSetupTextInstallRunner,
    SpeecherSetupTextGetOllama,
    SpeecherSetupTextDownloadWithOllama,
    SpeecherSetupTextEndpointModelHint,
};

// Mirrors speecher::TranscribeQueueState.
typedef NS_ENUM(NSInteger, SpeecherTranscribeQueueState) {
    SpeecherTranscribeQueueStateWaiting,
    SpeecherTranscribeQueueStateCurrent,
    SpeecherTranscribeQueueStateDone,
    SpeecherTranscribeQueueStateFailed,
};

// What a batch's results summary names its choices, captured when it starts.
// Mirrors speecher::TranscribeBatchLabels.
@interface SpeecherTranscribeBatchLabels : NSObject
@end

// The period Home's totals cover. Mirrors speecher::InsightsRange.
typedef NS_ENUM(NSInteger, SpeecherInsightsRange) {
    SpeecherInsightsRangeLast7Days,
    SpeecherInsightsRangeLast30Days,
    SpeecherInsightsRangeThisYear,
    SpeecherInsightsRangeAllTime,
};

// One day of the activity heatmap, with its colour level 0..4 under each
// measure the heatmap can show (speecher::HeatScale).
@interface SpeecherInsightsDayModel : NSObject
@property (nonatomic, readonly, copy) NSDate *date;
@property (nonatomic, readonly) NSInteger dictations;
@property (nonatomic, readonly) NSInteger words;
@property (nonatomic, readonly) NSInteger audioMs;
@property (nonatomic, readonly) NSInteger dictationsLevel;
@property (nonatomic, readonly) NSInteger wordsLevel;
@property (nonatomic, readonly) NSInteger audioLevel;
// The day's tip under each measure, dictations, words, audio
// (speecher::heatmapDayTip): its value, then the date on the next line.
@property (nonatomic, readonly, copy) NSArray<NSString *> *tips;
@end

// One row of "Where your words go". The "N other apps" fold has no profile.
@interface SpeecherInsightsAppModel : NSObject
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly, copy) NSString *profileLabel;
@property (nonatomic, readonly) NSInteger words;
@property (nonatomic, readonly) NSInteger percent;
@end

// One stat tile, as speecher::insightTiles words it.
@interface SpeecherInsightTileModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
// "text", "flame", "microphone" or "waveform" (speecher::InsightTileText).
@property (nonatomic, readonly, copy) NSString *iconId;
@property (nonatomic, readonly, copy) NSString *value;
@property (nonatomic, readonly, copy) NSString *unit;
@property (nonatomic, readonly, copy) NSArray<NSString *> *lines;
@property (nonatomic, readonly, copy) NSString *firstLineTip;
@property (nonatomic, readonly) BOOL showsWeek;
@end

// One row of the Records card (speecher::InsightRecordText).
@interface SpeecherInsightRecordModel : NSObject
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *detail;
@property (nonatomic, readonly, copy) NSString *value;
// The next milestone shows a progress bar of allTimeWords out of nextMilestone.
@property (nonatomic, readonly) BOOL milestoneBar;
@end

// Everything Home shows for one period, as speecher::summarize computed it.
// Days the page words relative to today ("yesterday", "Mar 1, 2026") arrive
// already worded, and are empty where the summary has no such day. So do the
// lines every Home shares (the speecher text helpers beside summarize).
@interface SpeecherInsightsModel : NSObject
// Every record in the log, whatever the period: 0 means Home has nothing yet.
@property (nonatomic, readonly) NSInteger recordCount;

// The period.
@property (nonatomic, readonly) NSInteger words;
@property (nonatomic, readonly) NSInteger dictations;

// Streak: seven BOOLs, Monday first, and today's slot among them.
@property (nonatomic, readonly, copy) NSArray<NSNumber *> *weekActivity;
@property (nonatomic, readonly) NSInteger todayIndex;
// One letter per weekday, Monday first (speecher::weekdayLetter).
@property (nonatomic, readonly, copy) NSArray<NSString *> *weekLetters;
// Words, streak, dictations, audio.
@property (nonatomic, readonly, copy) NSArray<SpeecherInsightTileModel *> *tiles;
// The heatmap's seven row labels, Monday first, empty but for Mon, Wed, Fri.
@property (nonatomic, readonly, copy) NSArray<NSString *> *heatmapRowLabels;
@property (nonatomic, readonly, copy) NSString *activeDaysLastYearText;
// Each hour's tip, "10 am to 11 am" then "3 dictations" on the next line.
@property (nonatomic, readonly, copy) NSArray<NSString *> *hourTips;
// "Morning talker." and the sentence after it.
@property (nonatomic, readonly, copy) NSString *personaText;
@property (nonatomic, readonly, copy) NSString *peakText;
// What Share copies and saves (speecher::insightsShareText, insightsJson),
// and the file name a save panel suggests.
@property (nonatomic, readonly, copy) NSString *shareText;
// The line under the shared image's figures, empty with no audio
// (speecher::insightsImagePaceLine).
@property (nonatomic, readonly, copy) NSString *imagePaceLine;
@property (nonatomic, readonly, copy) NSData *json;
@property (nonatomic, readonly, copy) NSString *jsonFileName;

// Every day of the last 53 Monday-first weeks up to today, oldest first.
@property (nonatomic, readonly, copy) NSArray<SpeecherInsightsDayModel *> *heatmap;
// One per heatmap week: the month named over it, or empty. Whatever the
// number of weeks shown, the labels are the tail of this list.
@property (nonatomic, readonly, copy) NSArray<NSString *> *weekMonthLabels;
// How strongly each heat level 0..4 shows the accent.
@property (nonatomic, readonly, copy) NSArray<NSNumber *> *heatStrengths;
@property (nonatomic, readonly) NSInteger activeDaysLastYear;

// The period by local hour: 24 counts, and each hour's label ("10 am").
@property (nonatomic, readonly, copy) NSArray<NSNumber *> *hourCounts;
@property (nonatomic, readonly, copy) NSArray<NSString *> *hourLabels;
@property (nonatomic, readonly) NSInteger peakHour;
@property (nonatomic, readonly) BOOL hasHourData;

@property (nonatomic, readonly) NSInteger wordsPerMinute;
@property (nonatomic, readonly) NSInteger typingWordsPerMinute;
// The time dictation saved over typing, "2 h 5 min" (speecher::minutesText).
@property (nonatomic, readonly, copy) NSString *minutesSavedText;
// "That's 3.6× faster than typing at 40 words per minute."
@property (nonatomic, readonly, copy) NSString *speedupText;

@property (nonatomic, readonly, copy) NSArray<SpeecherInsightsAppModel *> *apps;

// Records, all time.
@property (nonatomic, readonly) NSInteger allTimeWords;
// 0 once every milestone is passed.
@property (nonatomic, readonly) NSInteger nextMilestone;
@property (nonatomic, readonly, copy) NSArray<SpeecherInsightRecordModel *> *records;

// What each chart says to assistive technology (speecher::heatmapDescription,
// hourChartDescription, weekDescription); the heatmap's per measure, in the
// order dictations, words, audio.
@property (nonatomic, readonly, copy) NSArray<NSString *> *heatmapDescriptions;
@property (nonatomic, readonly, copy) NSString *hourChartDescription;
@property (nonatomic, readonly, copy) NSString *weekDescription;
@end

// ModelRating, which the rating badge colours by.
typedef NS_ENUM(NSInteger, SpeecherModelRating) {
    SpeecherModelRatingRecommended,
    SpeecherModelRatingGood,
    SpeecherModelRatingSituational,
    SpeecherModelRatingNotRecommended,
};

// One Local Model as the Local models page and the setup assistant show it:
// the catalog's facts in core's words, and where this computer stands with it.
@interface LocalModelInfo : NSObject
@property (nonatomic, readonly, copy) NSString *modelId;
@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, readonly, copy) NSString *fileName;
// "731 MB", as download sizes are quoted, and its button, "Download 731 MB".
@property (nonatomic, readonly, copy) NSString *sizeText;
@property (nonatomic, readonly, copy) NSString *downloadCaption;
// Word error rates in percent.
@property (nonatomic, readonly) double librispeechWer;
@property (nonatomic, readonly) double fleursWer;
// Text appears as the person speaks rather than after they stop, and that
// worded (speecher::textShowsValue).
@property (nonatomic, readonly) BOOL streams;
@property (nonatomic, readonly, copy) NSString *textShowsText;
// "English", or "14 languages" (speecher::languagesValue).
@property (nonatomic, readonly, copy) NSString *languagesText;
@property (nonatomic, readonly, copy) NSString *licence;
@property (nonatomic, readonly) SpeecherModelRating rating;
// The badge's words, "Recommended" to "Not recommended".
@property (nonatomic, readonly, copy) NSString *ratingLabel;
// Who should pick it, in one short line.
@property (nonatomic, readonly, copy) NSString *bestFor;
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
// The setup card's three lines, and the comparison table's six cells.
@property (nonatomic, readonly, copy) NSString *cardFacts;
@property (nonatomic, readonly, copy) NSArray<NSString *> *tableCells;
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

// The Local Model detail's fact names, one property per LocalModelFactLabels
// field, so the Swift side pairs each with its value by name.
@interface LocalModelFactNames : NSObject
@property (nonatomic, readonly, copy) NSString *bestFor;
@property (nonatomic, readonly, copy) NSString *download;
@property (nonatomic, readonly, copy) NSString *speedHere;
@property (nonatomic, readonly, copy) NSString *wordErrorRate;
@property (nonatomic, readonly, copy) NSString *textShows;
@property (nonatomic, readonly, copy) NSString *language;
@property (nonatomic, readonly, copy) NSString *license;
@end

// Everything LocalSetup knows, read in one go on every change it announces.
@interface LocalSetupState : NSObject
// "Apple M4 Max, 14 threads · Apple M4 Max, Metal · 36 GB memory", or
// "Checking this computer…" until the probe answers.
@property (nonatomic, readonly, copy) NSString *hardwareLine;
@property (nonatomic, readonly) BOOL hardwareKnown;
// Where the word error rates come from, for a tooltip on them.
@property (nonatomic, readonly, copy) NSString *wordErrorRateSources;
@property (nonatomic, readonly, strong) LocalModelFactNames *factLabels;
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
// What a status line says about dictation now (speecher::dictationStatusLabel).
@property (nonatomic, readonly, copy) NSString *statusLabel;
@property (nonatomic, readonly) SpeecherDictationState dictationState;
// Every dictation state change, carrying the new status label.
@property (nonatomic, copy, nullable) void (^statusChanged)(NSString *status);
// What the Start/Stop control says and whether it does anything now
// (speecher::dictationToggleAction), and whether the microphone is open
// (speecher::dictationListeningPresentation).
@property (nonatomic, readonly, copy) NSString *toggleLabel;
@property (nonatomic, readonly) BOOL toggleEnabled;
@property (nonatomic, readonly) BOOL listening;
// The captions the menu bar panel shares with the Linux and Windows trays.
@property (nonatomic, readonly, copy) NSString *traySettingsCaption;
// The notice while Accessibility is off (speecher::accessibilityNoticeText).
- (NSString *)accessibilityNoticeText:(BOOL)compact NS_SWIFT_NAME(accessibilityNoticeText(compact:));
// The menu bar item's accessible name (speecher::trayToolTip).
- (NSString *)trayToolTip:(BOOL)listening NS_SWIFT_NAME(trayToolTip(listening:));
@property (nonatomic, readonly, copy) NSString *trayQuitCaption;
// A level meter's accessible name, in the menu bar panel and setup.
@property (nonatomic, readonly, copy) NSString *inputLevelLabel;
@property (nonatomic, readonly, copy) NSString *copyTranscriptCaption;
// What a copy button says for a moment after copying, and for how long.
@property (nonatomic, readonly, copy) NSString *copiedCaption;
@property (class, nonatomic, readonly) NSTimeInterval copiedFeedbackSeconds;
// Why the last session failed, until the next one starts, except in the error
// state, whose status already says it (speecher::dictationFailureNote).
// Re-read on every statusChanged.
@property (nonatomic, readonly, copy) NSString *failureNote;
// Home's fixed wording (speecher::InsightsShareLabels, speecher::homeText
// and the heatmap's measures), keyed "share", "copyImage", "imageTitle",
// "copyText", "saveJson",
// "copied", "saved", "saveFailed", "saveTitle", "correctionsTitle",
// "legendLess", "legendMore", "insightsOffTitle", "insightsOffBody",
// "noInsightsTitle", "noInsightsBody", "insightsSettings", "yourDictation",
// "period", "activity", "measure", "whenYouTalk", "noHourData", "pace",
// "speakingPace", "savedOverTyping", "youSpeaking", "typicalTyping",
// "whereYourWordsGo", "noDictationInPeriod", "records", "privacyNote",
// "clearHistoryQuestion", "clearHistoryBody", "clearHistoryConfirm",
// "clearHistoryFailed",
// "measureDictations", "measureWords", "measureAudio".
@property (nonatomic, readonly, copy) NSDictionary<NSString *, NSString *> *homeLabels;
- (NSString *)learnedCorrectionsCaption:(NSInteger)count NS_SWIFT_NAME(learnedCorrectionsCaption(_:));
// The line under the learned corrections count and the button under it,
// empty when there is nothing to open (speecher::learnedCorrectionsNote and
// learnedCorrectionsAction, from the settings and the Accessibility grant).
@property (nonatomic, readonly, copy) NSString *learnedCorrectionsNote;
@property (nonatomic, readonly, copy) NSString *learnedCorrectionsAction;
// "Last 30 days".
- (NSString *)insightsRangeLabel:(SpeecherInsightsRange)range NS_SWIFT_NAME(insightsRangeLabel(_:));
// The line under Home's status (speecher::dictationShortcutHint).
- (NSString *)dictationShortcutHint:(NSString *)shortcut NS_SWIFT_NAME(dictationShortcutHint(_:));
@property (nonatomic, readonly, copy) NSString *noTranscriptYetText;
- (void)quit;
@property (nonatomic, copy, nullable) void (^audioLevelChanged)(float level);
// A capability the schema gates rows on moved — the Accessibility grant, or
// whether this computer took the last launch-at-login change — so the settings
// pages are worth re-reading.
@property (nonatomic, copy, nullable) void (^accessibilityChanged)(void);
@property (nonatomic, readonly) BOOL whatsNewPending;
@property (nonatomic, copy, nullable) void (^whatsNewChanged)(void);
- (void)clearPendingWhatsNew;

// The update banner, the same for the settings window and the panel's chip,
// re-read whole on every updateChanged.
@property (nonatomic, readonly, strong) SpeecherUpdateBanner *updateBanner;
@property (nonatomic, readonly, strong) SpeecherWhatsNewBanner *whatsNewBanner;
@property (nonatomic, copy, nullable) void (^updateChanged)(void);
// The banner's buttons (speecher::UpdateBanner).
- (void)runUpdateAction;
- (void)deferUpdate;
- (void)dismissUpdate;
// The running version, which the settings show.
@property (nonatomic, readonly, copy) NSString *installedVersion;
- (void)toggle;
- (void)startListening;
- (void)stopListening;
// Throws the dictation away (speecher::ApplicationController::cancel), and
// pauses it or resumes it while paused (speecher::DictationSession).
- (void)cancel;
- (void)togglePause;
// The buttons either side of the waveform for the current state
// (speecher::sessionControls), and whether Cancel belongs in the menu bar
// panel (speecher::dictationCancelable). Re-read on every statusChanged.
@property (nonatomic, readonly) BOOL pauseVisible;
@property (nonatomic, readonly) BOOL pauseEnabled;
@property (nonatomic, readonly) BOOL paused;
@property (nonatomic, readonly) BOOL cancelVisible;
@property (nonatomic, readonly) BOOL cancelable;
@property (class, nonatomic, readonly, copy) NSString *pauseCaption;
@property (class, nonatomic, readonly, copy) NSString *resumeCaption;
@property (class, nonatomic, readonly, copy) NSString *cancelCaption;
@property (class, nonatomic, readonly, copy) NSString *cancelDictationCaption;

// The dictation panel's own state. It is a floating window rather than a
// settings pane, so it reads these rather than the schema.
@property (nonatomic, copy, nullable) void (^popupShowRequested)(uint64_t generation);
@property (nonatomic, copy, nullable) void (^popupHideRequested)(void);
@property (nonatomic, copy, nullable) void (^popupStatusChanged)(NSString *status, SpeecherDictationState state);
@property (nonatomic, copy, nullable) void (^popupPreviewChanged)(NSString *preview);
@property (nonatomic, copy, nullable) void (^popupFrozenChanged)(BOOL frozen);
@property (nonatomic, copy, nullable) void (^popupRefiningChanged)(BOOL refining);
// The last words of the refinement stream, emitted only while refining.
@property (nonatomic, copy, nullable) void (^popupRefinementPreviewChanged)(NSString *preview);
@property (nonatomic, copy, nullable) void (^popupOAuthRefreshRequested)(void);
@property (nonatomic, copy, nullable) void (^popupListeningIndicatorRequested)(void);
@property (nonatomic, copy, nullable) void (^popupErrorRequested)(NSString *message, SpeecherErrorAction *fix);
// A delivery's receipt, with the outcome that picks its symbol and what it
// offers to fix, such as an expired sign-in a fallback stood in for.
@property (nonatomic, copy, nullable) void (^popupMessageRequested)(NSString *message,
                                                                   SpeecherPopupOutcome outcome,
                                                                   SpeecherErrorAction *fix);
// speecher::checkingCredentialsStatus() and accessibilityGrantActionLabel().
@property (class, nonatomic, readonly, copy) NSString *checkingCredentialsStatus;
@property (class, nonatomic, readonly, copy) NSString *accessibilityGrantActionLabel;
// speecher::globalShortcutPrompt(), the row's Change caption, and its Reset
// caption naming the default.
@property (class, nonatomic, readonly, copy) NSString *globalShortcutPrompt;
@property (class, nonatomic, readonly, copy) NSString *globalShortcutChangeCaption;
+ (NSString *)globalShortcutResetCaption:(NSString *)defaultShortcut NS_SWIFT_NAME(globalShortcutResetCaption(_:));
// What settings search shows when nothing matches (speecher::noSettingsMatchText).
@property (class, nonatomic, readonly, copy) NSString *noSettingsMatchText;
// The settings window's Back caption (speecher::settingsBackCaption).
@property (class, nonatomic, readonly, copy) NSString *settingsBackCaption;
// The popup's captions (speecher::popupDismissCaption, renewingSignInText,
// and dictationStatusLabel for a state).
@property (class, nonatomic, readonly, copy) NSString *popupDismissCaption;
@property (class, nonatomic, readonly, copy) NSString *renewingSignInText;
+ (NSString *)statusLabelFor:(SpeecherDictationState)state NS_SWIFT_NAME(statusLabel(for:));
// The popup's buttons for the state its status was reported with.
+ (SpeecherSessionControls *)sessionControlsFor:(SpeecherDictationState)state NS_SWIFT_NAME(sessionControls(for:));
// speecher::kPopupErrorWrapWidth, and popupErrorDismissMs for a message.
@property (class, nonatomic, readonly) CGFloat popupErrorWrapWidth;
+ (NSTimeInterval)popupErrorDismissSecondsFor:(NSString *)message NS_SWIFT_NAME(popupErrorDismissSeconds(for:));
// speecher::trimPreviewToFit against the panel's own font and width.
+ (SpeecherPreviewLine *)trimPreview:(NSString *)preview toWidth:(CGFloat)width font:(NSFont *)font
    NS_SWIFT_NAME(trimPreview(_:toWidth:font:));
// The panel is on screen, so the session need not wait out its fallback timer
// before opening the microphone.
- (void)notePopupPresented:(uint64_t)generation NS_SWIFT_NAME(notePopupPresented(generation:));

// The last transcript Speecher heard, which the menu bar panel offers to copy
// again (ApplicationController::lastTranscript). Empty until one exists.
@property (nonatomic, readonly, copy) NSString *lastTranscript;
@property (nonatomic, copy, nullable) void (^transcriptChanged)(NSString *transcript);
// The last transcript's word count (speecher::countWords).
@property (nonatomic, readonly) NSInteger lastTranscriptWords;
// The app and relative day insights recorded for the last delivered
// transcript (ApplicationController::lastRecord). Empty strings with none.
@property (nonatomic, readonly, copy) NSString *lastRecordApp;
@property (nonatomic, readonly, copy) NSString *lastRecordDay;
@property (nonatomic, copy, nullable) void (^lastRecordChanged)(void);

// Home's insights. The setting says whether new dictations are recorded; the
// log keeps what it has either way until it is cleared.
@property (nonatomic, readonly) BOOL insightsEnabled;
// The log gained or lost records.
@property (nonatomic, copy, nullable) void (^insightsChanged)(void);
- (SpeecherInsightsModel *)insightsSummaryForRange:(SpeecherInsightsRange)range
    NS_SWIFT_NAME(insightsSummary(range:));
// Deletes every record, once the person has confirmed it. NO when the
// history file could not be deleted.
- (BOOL)clearInsights;
@property (nonatomic, readonly) NSInteger learnedCorrectionCount;

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
// The built-in default, as shortcutDisplay would write it, and binding it.
@property (nonatomic, readonly, copy) NSString *defaultShortcutDisplay;
- (nullable NSString *)resetShortcut;
// A registered hotkey is consumed system-wide and never reaches a recorder's
// key monitor: pressing the bound combination while recording would start
// dictation instead of re-recording it. Recording therefore lets go of the
// registration first; ending it restores the bound sequence.
- (void)beginShortcutRecording;
// A shortcut that could not be taken back returns its error, with
// cancelShortcutFailed, when given, saying whether it was the Cancel Shortcut's.
- (nullable NSString *)endShortcutRecordingCancelShortcutFailed:(BOOL *_Nullable)cancelShortcutFailed
    NS_SWIFT_NAME(endShortcutRecording(cancelShortcutFailed:));

// The Cancel Shortcut: the same recorder's two kinds of binding, and Clear,
// since it has no default. Each returns nil once done, otherwise why not.
@property (nonatomic, readonly, copy) NSString *cancelShortcutDisplay;
- (nullable NSString *)bindCancelShortcutWithCharacters:(NSString *)characters
                                          modifierFlags:(NSUInteger)modifierFlags
    NS_SWIFT_NAME(bindCancelShortcut(characters:modifierFlags:));
- (nullable NSString *)bindCancelSingleKeyCode:(NSString *)code
    NS_SWIFT_NAME(bindCancelSingleKey(code:));
- (nullable NSString *)clearCancelShortcut;
@property (class, nonatomic, readonly, copy) NSString *globalShortcutClearCaption;
@property (class, nonatomic, readonly, copy) NSString *globalShortcutUnsetText;

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

// The steps, and the wording every assistant shares (app/SetupSteps.h).
@property (nonatomic, readonly, copy) NSArray<SpeecherSetupStep *> *setupSteps;
@property (nonatomic, readonly, copy) NSString *setupWindowTitle;
// The microphone choice's system-default entry.
@property (nonatomic, readonly, copy) NSString *audioDeviceDefaultLabel;
@property (nonatomic, readonly, copy) NSString *setupSilentMicrophoneHint;
@property (nonatomic, readonly, copy) NSString *setupBlockedHeading;
@property (nonatomic, readonly, copy) NSString *setupBlockedFooter;
- (NSString *)setupStepCounter:(NSInteger)step of:(NSInteger)total NS_SWIFT_NAME(setupStepCounter(_:of:));
// The Transcription step's dead-end note, or empty while any way into
// dictation remains; core decides when it shows and words it.
- (NSString *)setupTranscriptionDeadEnd:(BOOL)signInFound
                            localUsable:(BOOL)localUsable
                          endpointSaved:(BOOL)endpointSaved
              signInProvidersRegistered:(BOOL)signInProvidersRegistered
    NS_SWIFT_NAME(setupTranscriptionDeadEnd(signInFound:localUsable:endpointSaved:signInProvidersRegistered:));
- (NSString *)setupTranscriptionBlocked:(BOOL)localSelected provider:(NSString *)providerLabel
    NS_SWIFT_NAME(setupTranscriptionBlocked(localSelected:provider:));
- (NSString *)setupMicrophoneBlocked:(BOOL)accessGranted NS_SWIFT_NAME(setupMicrophoneBlocked(accessGranted:));
- (NSString *)setupReadyIntro:(BOOL)blocked downloading:(BOOL)downloading
    NS_SWIFT_NAME(setupReadyIntro(blocked:downloading:));
- (NSString *)setupActivationInstruction:(NSString *)shortcut NS_SWIFT_NAME(setupActivationInstruction(shortcut:));
- (NSString *)setupProviderVerdict:(NSString *)providerId ready:(BOOL)ready
    NS_SWIFT_NAME(setupProviderVerdict(_:ready:));
// ready: nil while no probe has answered.
- (NSString *)setupRefinementStatus:(NSString *)providerId ready:(nullable NSNumber *)ready
    NS_SWIFT_NAME(setupRefinementStatus(_:ready:));
- (NSString *)setupChecklistLine:(NSString *)stepId choice:(NSString *)choice
    NS_SWIFT_NAME(setupChecklistLine(_:choice:));
- (NSString *)setupText:(SpeecherSetupText)text NS_SWIFT_NAME(setupText(_:));
// The Local models page's and the Local Model card's wording
// (speecher::localModelText, compareModelsCaption, compareTableHeaders).
+ (NSString *)localModelText:(SpeecherLocalModelText)text NS_SWIFT_NAME(localModelText(_:));
+ (NSString *)compareModelsCaption:(NSInteger)otherModels NS_SWIFT_NAME(compareModelsCaption(_:));
+ (NSString *)deleteModelQuestion:(NSString *)modelName NS_SWIFT_NAME(deleteModelQuestion(_:));
@property (class, nonatomic, readonly, copy) NSArray<NSString *> *compareTableHeaders;
// A settings row's label and choices as the Settings window words them
// (speecher::setupSchemaRow), whether or not the row shows right now.
- (NSString *)setupRowLabel:(NSString *)rowId NS_SWIFT_NAME(setupRowLabel(_:));
- (NSArray<RowOptionModel *> *)setupRowOptions:(NSString *)rowId NS_SWIFT_NAME(setupRowOptions(_:));
- (NSString *)setupProviderReady:(NSString *)providerLabel NS_SWIFT_NAME(setupProviderReady(_:));
- (NSString *)setupRefinementNotSignedIn:(NSString *)providerLabel NS_SWIFT_NAME(setupRefinementNotSignedIn(_:));
- (NSString *)setupLocalSpeechChoice:(NSString *)modelName NS_SWIFT_NAME(setupLocalSpeechChoice(_:));
- (NSString *)setupCliproxySpeechChoice:(NSString *)providerLabel NS_SWIFT_NAME(setupCliproxySpeechChoice(_:));
// The Ready step's verdict on pasting: Ready, or Clipboard only.
- (NSString *)setupPasteVerdict:(BOOL)pastes NS_SWIFT_NAME(setupPasteVerdict(_:));
// The fallbacks section under the chosen provider on the Transcription or
// Refinement step, from the saved settings, and its suggestion's action
// (LocalSetup::acceptSetupFallbackOffer), which adds the suggested fallback.
- (SpeecherSetupFallbackSection *)setupFallbackSection:(SpeecherProviderRole)role
    NS_SWIFT_NAME(setupFallbackSection(_:));
- (void)acceptSetupFallbackOffer:(SpeecherProviderRole)role NS_SWIFT_NAME(acceptSetupFallbackOffer(_:));

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
// Core's setup policy: the speech provider auto-selection may move to.
- (NSString *)setupSpeechChoiceForSaved:(NSString *)saved
                         readyProviders:(NSArray<NSString *> *)readyProviders
                           localOffered:(BOOL)localOffered
                      proxyAccountFound:(BOOL)proxyAccountFound
    NS_SWIFT_NAME(setupSpeechChoice(saved:readyProviders:localOffered:proxyAccountFound:));
// Core's refinement default for the saved provider; nil while the runner
// check is still looking.
- (nullable NSString *)setupRefinementChoiceForSaved:(NSString *)saved
                                      readyProviders:(NSArray<NSString *> *)readyProviders
    NS_SWIFT_NAME(setupRefinementChoice(saved:readyProviders:));
- (BOOL)offersSetupSpeechProvider:(NSString *)providerId saved:(NSString *)saved localAvailable:(BOOL)localAvailable
    NS_SWIFT_NAME(offersSetupSpeechProvider(_:saved:localAvailable:));
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

// The Test microphone row's test (speecher::MicrophoneTest), apart from the
// meter above, so the setup assistant and the row never share a device. It
// ends itself when a Dictation Session starts or another device is saved.
@property (nonatomic, readonly) SpeecherMicrophoneTestState microphoneTestState;
// The button's caption (speecher::microphoneTestCaption) and whether it may
// be clicked (speecher::MicrophoneTest::canToggle).
@property (nonatomic, readonly, copy) NSString *microphoneTestCaption;
@property (nonatomic, readonly) BOOL microphoneTestEnabled;
// Each called on the main thread: the state or enabled moved, a level, and
// why the device would not open.
@property (nonatomic, copy, nullable) void (^microphoneTestChanged)(void);
@property (nonatomic, copy, nullable) void (^microphoneTestLevelChanged)(float level);
@property (nonatomic, copy, nullable) void (^microphoneTestFailed)(NSString *message);
// Starts the test on the saved Input device, or stops it.
- (void)toggleMicrophoneTest;
- (void)stopMicrophoneTest;
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
@property (nonatomic, readonly, strong) SpeecherCredentialStatus *credentialStatus;
@property (nonatomic, readonly, strong) SpeecherCredentialStatus *anthropicCredentialStatus;
@property (nonatomic, copy, nullable) void (^anthropicCredentialsChanged)(void);
- (NSString *)readApiKey;
// nil when the keyring took it, otherwise why it refused.
- (nullable NSString *)saveApiKey:(NSString *)apiKey;

// Running models on this computer: ApplicationController's LocalSetup, so a
// download, a Speed Test or a pull outlives the window that started it.
// Whether this build can run speech on this computer at all.
@property (nonatomic, readonly) BOOL localSpeechAvailable;
// Whether the hardware can run any catalog model. Optimistically YES until
// the hardware probe answers.
@property (nonatomic, readonly) BOOL localSpeechCanRun;
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

// Transcribing audio files: the Transcribe pane's seams into the core's
// FileTranscriptionSession. One batch runs at a time, and it and dictation
// exclude each other.

// The built-in dictation prompt at Medium cleanup with no tone, which the
// custom system prompt resets to.
@property (nonatomic, readonly, copy) NSString *builtInSystemPrompt;

// The same lists the Qt Transcribe page offers. Refinement's "None" is the
// pane's own first choice and is not in refinementProviders.
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *cleanupStrengths;
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *writingTones;
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *writingProfiles;
// Choices seeded from the user's settings, with the cleanup strength and tone
// of the named writing profile as the user set it up; nil names the default
// profile. Never written back to the settings.
- (SpeecherTranscribeOptions *)transcribeOptionsWithWritingProfile:(nullable NSString *)profile
    NS_SWIFT_NAME(transcribeOptions(writingProfile:));
// The model a refinement provider is set to use, or empty for none.
- (NSString *)refinementModelForProvider:(NSString *)providerId
    NS_SWIFT_NAME(refinementModel(provider:));
// The extensions the file chooser offers, audio and video, without dots.
@property (nonatomic, readonly, copy) NSArray<NSString *> *transcribableExtensions;
// Under the file chooser while no files are listed.
@property (nonatomic, readonly, copy) NSString *mediaFilesHint;
@property (nonatomic, readonly, copy) NSString *mediaFilesTooltip;
// The paths among these that are files the decoder can take.
- (NSArray<NSString *> *)audioFilesAmong:(NSArray<NSString *> *)paths
    NS_SWIFT_NAME(audioFiles(among:));
// nil once started; otherwise why not, such as a dictation under way.
- (nullable NSString *)startTranscribingFiles:(NSArray<NSString *> *)paths
                                      options:(SpeecherTranscribeOptions *)options
    NS_SWIFT_NAME(startTranscribing(files:options:));
// Stops the current file and skips the rest; the batch still finishes.
- (void)cancelTranscription;
// Writes text as "<name>-transcribed.txt" in folder, numbered rather than
// overwriting. nil once written, otherwise why not.
- (nullable NSString *)saveTranscript:(NSString *)text
                          forAudioFile:(NSString *)audioPath
                              inFolder:(NSString *)folder
    NS_SWIFT_NAME(saveTranscript(_:forAudioFile:inFolder:));
// The Transcribe pane's wording, shared with the Qt and Windows front ends
// (speecher/transcribe/TranscribePresentation.h). A negative length is unknown.
- (NSString *)transcribePhaseLabel:(SpeecherTranscribePhase)phase NS_SWIFT_NAME(phaseLabel(_:));
- (NSString *)transcribeStepLabel:(SpeecherTranscribeStep)step NS_SWIFT_NAME(stepLabel(_:));
// The line under the step indicator; empty for a step that needs none.
- (NSString *)transcribeStepHint:(SpeecherTranscribeStep)step NS_SWIFT_NAME(stepHint(_:));
- (NSString *)transcribeText:(SpeecherTranscribeText)text NS_SWIFT_NAME(text(_:));
- (NSString *)chooseFilesCaption:(BOOL)anyListed NS_SWIFT_NAME(chooseFilesCaption(anyListed:));
- (NSString *)transcribeStartCaption:(NSInteger)fileCount NS_SWIFT_NAME(startCaption(fileCount:));
- (NSString *)transcribeResultsTitle:(NSInteger)resultCount NS_SWIFT_NAME(resultsTitle(count:));
- (NSString *)destinationLabel:(SpeecherTranscriptDestination)destination NS_SWIFT_NAME(destinationLabel(_:));
- (NSString *)destinationHint:(SpeecherTranscriptDestination)destination NS_SWIFT_NAME(destinationHint(_:));
- (NSString *)transcriptSaveErrorForPath:(NSString *)path reason:(NSString *)reason
    NS_SWIFT_NAME(transcriptSaveError(path:reason:));
- (NSString *)percentLabel:(double)progress NS_SWIFT_NAME(percentLabel(_:));
- (NSString *)percentSpoken:(double)progress NS_SWIFT_NAME(percentSpoken(_:));
// The settings page a row is on, from core (speecher::paneTitleForRow).
- (NSString *)paneTitleForRowId:(NSString *)rowId NS_SWIFT_NAME(paneTitle(forRowId:));
// Where to change the refinement model the Configure step shows.
@property (nonatomic, readonly, copy) NSString *transcribeRefinementModelHint;
// How long a finished file holds at the end before the next one replaces it.
@property (nonatomic, readonly) double transcribeLandingSeconds;
// A file's share of its whole work, below 1 until the file has finished.
- (double)overallFileProgress:(double)fractionSent
                        phase:(SpeecherTranscribePhase)phase
                      refines:(BOOL)refines
                    msInPhase:(int64_t)msInPhase
    NS_SWIFT_NAME(overallFileProgress(fractionSent:phase:refines:msInPhase:));
- (NSString *)durationLabel:(int64_t)durationMs NS_SWIFT_NAME(durationLabel(_:));
- (NSString *)audioFileDetailWithBytes:(int64_t)bytes durationMs:(int64_t)durationMs
    NS_SWIFT_NAME(audioFileDetail(bytes:durationMs:));
- (BOOL)refinesTranscripts:(SpeecherTranscribeOptions *)options NS_SWIFT_NAME(refinesTranscripts(_:));
- (NSString *)shownTranscript:(SpeecherTranscriptResult *)result raw:(BOOL)raw
    NS_SWIFT_NAME(shownTranscript(_:raw:));
- (NSString *)resultMeta:(SpeecherTranscriptResult *)result durationMs:(int64_t)durationMs raw:(BOOL)raw
    NS_SWIFT_NAME(resultMeta(_:durationMs:raw:));
- (NSString *)allTranscripts:(NSArray<SpeecherTranscriptResult *> *)results raw:(BOOL)raw
    NS_SWIFT_NAME(allTranscripts(_:raw:));
// A transcript's Export menu: each format's caption and file extension,
// whether the result offers it, and what it writes.
- (NSString *)transcriptFormatCaption:(SpeecherTranscriptFormat)format NS_SWIFT_NAME(formatCaption(_:));
- (NSString *)transcriptFileExtension:(SpeecherTranscriptFormat)format NS_SWIFT_NAME(fileExtension(_:));
- (BOOL)canExport:(SpeecherTranscriptResult *)result as:(SpeecherTranscriptFormat)format
    NS_SWIFT_NAME(canExport(_:as:));
- (NSString *)exportedTranscript:(SpeecherTranscriptResult *)result
                          format:(SpeecherTranscriptFormat)format
                             raw:(BOOL)raw
    NS_SWIFT_NAME(exportedTranscript(_:format:raw:));
- (NSString *)processingTitleForBatch:(NSArray<NSString *> *)batch current:(NSInteger)current
    NS_SWIFT_NAME(processingTitle(batch:current:));
- (SpeecherTranscribeQueueState)queueStateAt:(NSInteger)index
                                     current:(NSInteger)current
                                    finished:(NSArray<SpeecherTranscriptResult *> *)finished
    NS_SWIFT_NAME(queueState(at:current:finished:));
- (NSString *)queueStateLabel:(SpeecherTranscribeQueueState)state phase:(NSString *)phase
    NS_SWIFT_NAME(queueStateLabel(_:phase:));
- (SpeecherTranscribeBatchLabels *)batchLabelsForOptions:(SpeecherTranscribeOptions *)options
    NS_SWIFT_NAME(batchLabels(for:));
- (NSString *)batchSummaryForResults:(NSArray<SpeecherTranscriptResult *> *)results
                           batchSize:(NSInteger)batchSize
                           cancelled:(BOOL)cancelled
                           durations:(NSDictionary<NSString *, NSNumber *> *)durationsMs
                             options:(SpeecherTranscribeOptions *)options
                              labels:(SpeecherTranscribeBatchLabels *)labels
    NS_SWIFT_NAME(batchSummary(results:batchSize:cancelled:durations:options:labels:));
// The line under the summary about subtitles; empty when none finished.
- (NSString *)subtitlesNoteForResults:(NSArray<SpeecherTranscriptResult *> *)results
                               labels:(SpeecherTranscribeBatchLabels *)labels
    NS_SWIFT_NAME(subtitlesNote(results:labels:));
// The batch as it runs, on the main thread. Indexes count files in the order
// they were passed to startTranscribing.
@property (nonatomic, copy, nullable) void (^transcriptionBatchStarted)(NSInteger count);
@property (nonatomic, copy, nullable) void (^transcriptionFileStarted)(NSInteger index, NSString *path);
// The decoded file's peak levels (0 to 1) across its length, and its length.
@property (nonatomic, copy, nullable) void (^transcriptionFileDecoded)
    (NSInteger index, NSArray<NSNumber *> *peaks, int64_t durationMs);
@property (nonatomic, copy, nullable) void (^transcriptionFileProgress)(NSInteger index, double fraction);
// The transcript so far, whole each time.
@property (nonatomic, copy, nullable) void (^transcriptionFilePartial)(NSInteger index, NSString *text);
@property (nonatomic, copy, nullable) void (^transcriptionFileRefining)(NSInteger index);
@property (nonatomic, copy, nullable) void (^transcriptionFileFinished)
    (NSInteger index, SpeecherTranscriptResult *result);
// Every file that finished or failed; a cancelled one is left out.
@property (nonatomic, copy, nullable) void (^transcriptionBatchFinished)
    (NSArray<SpeecherTranscriptResult *> *results, BOOL cancelled);
@end

#ifdef __cplusplus
namespace speecher {
class ApplicationController;
struct PopupErrorAction;
}

@interface SpeecherBridge (Cxx)
- (instancetype)initWithController:(speecher::ApplicationController *)controller;
@end

@interface SpeecherErrorAction (Cxx)
+ (SpeecherErrorAction *)actionWithCore:(const speecher::PopupErrorAction &)action;
@end
#endif

NS_ASSUME_NONNULL_END
