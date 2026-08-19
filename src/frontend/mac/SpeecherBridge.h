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

@interface RowOptionModel : NSObject
@property (nonatomic, readonly, copy) NSString *rowOptionId;
@property (nonatomic, readonly, copy) NSString *label;
@property (nonatomic, readonly) BOOL enabled;
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
// Number row, and nil for a row that holds no value of its own.
@property (nonatomic, readonly, strong, nullable) id value;
@property (nonatomic, readonly, copy) NSArray<RowOptionModel *> *options;
@property (nonatomic, readonly) BOOL enabled;
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
@end

@interface SpeecherBridge : NSObject
@property (nonatomic, readonly, strong) SettingsSchemaModel *settingsSchema;
@property (nonatomic, readonly, copy) NSString *stateName;
@property (nonatomic, copy, nullable) void (^statusChanged)(NSString *status);
@property (nonatomic, copy, nullable) void (^audioLevelChanged)(float level);
- (void)toggle;
- (void)startListening;
- (void)stopListening;
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
