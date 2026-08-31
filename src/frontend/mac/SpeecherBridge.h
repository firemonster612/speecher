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
// Set on a Collection row, and on the one Custom row that is a table.
@property (nonatomic, readonly, strong, nullable) CollectionModel *collection;
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
// What an Action row's button does. The schema names the commands; what they do
// belongs to the front end, as it does on Qt.
@property (nonatomic, copy, nullable) void (^actionTriggered)(NSString *rowId);
- (void)setValue:(nullable id)value forRowId:(NSString *)rowId;
// Writes the draft back to the store and re-reads it.
- (void)commit;
// Lets the rows whose choices are slow to gather — a device enumeration —
// offer them from now on. Called once the window has painted.
- (void)loadExpensiveRows;
// Empty when these records are consistent; otherwise one message per problem.
- (NSArray<NSString *> *)problemsWith:(NSArray<SpeecherRecord *> *)records forRowId:(NSString *)rowId;
- (CollectionImportResult *)recordsImportedFrom:(NSData *)data
                                           into:(NSArray<SpeecherRecord *> *)records
                                       forRowId:(NSString *)rowId;
// What a cell says on hover, which a learned correction answers per record.
- (NSString *)tooltipForColumn:(NSString *)columnId
                      inRowId:(NSString *)rowId
                       record:(SpeecherRecord *)record;
@end

@interface SpeecherBridge : NSObject
@property (nonatomic, readonly, strong) SettingsSchemaModel *settingsSchema;
@property (nonatomic, readonly, copy) NSString *stateName;
@property (nonatomic, copy, nullable) void (^statusChanged)(NSString *status);
@property (nonatomic, copy, nullable) void (^audioLevelChanged)(float level);
@property (nonatomic, copy, nullable) void (^accessibilityChanged)(void);
- (void)toggle;
- (void)startListening;
- (void)stopListening;

// The dictation panel's own state. It is a floating window rather than a
// settings pane, so it reads these rather than the schema.
@property (nonatomic, copy, nullable) void (^popupShowRequested)(uint64_t generation);
@property (nonatomic, copy, nullable) void (^popupHideRequested)(void);
@property (nonatomic, copy, nullable) void (^popupStatusChanged)(NSString *status);
@property (nonatomic, copy, nullable) void (^popupPreviewChanged)(NSString *preview);
@property (nonatomic, copy, nullable) void (^popupRefiningChanged)(BOOL refining);
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

@property (nonatomic, readonly) BOOL accessibilitySupported;
@property (nonatomic, readonly) BOOL accessibilityEnabled;
// nil once the grant was asked for; otherwise why it could not be.
- (nullable NSString *)enableAccessibility;

// The OpenAI credential as the Providers page shows it: a status line for every
// credential source except the app settings key, which is a secret this front
// end reads from and writes to the keyring itself.
@property (nonatomic, readonly) BOOL credentialIsEditable;
@property (nonatomic, readonly, copy) NSString *credentialStatus;
- (NSString *)readApiKey;
// nil when the keyring took it, otherwise why it refused.
- (nullable NSString *)saveApiKey:(NSString *)apiKey;
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
