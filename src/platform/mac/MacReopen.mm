#include "platform/mac/MacReopen.h"

#include <QObject>
#include <QTimer>

#import <AppKit/AppKit.h>

#include <utility>

@interface SpeecherReopenHandler : NSObject {
    std::function<void()> _reopen;
}
- (instancetype)initWithReopen:(std::function<void()>)reopen;
- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply;
@end

@implementation SpeecherReopenHandler

- (instancetype)initWithReopen:(std::function<void()>)reopen
{
    if ((self = [super init])) {
        _reopen = std::move(reopen);
    }
    return self;
}

- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply
{
    Q_UNUSED(event);
    Q_UNUSED(reply);
    _reopen();
}

@end

namespace speecher::mac {

void onReopen(QObject *context, std::function<void()> reopen)
{
    // The event manager does not retain its handlers.
    SpeecherReopenHandler *handler = [[SpeecherReopenHandler alloc] initWithReopen:std::move(reopen)];
    // Once the event loop runs: AppKit installs its own reopen handler, the
    // one that would only tell Qt, as the application finishes launching.
    QTimer::singleShot(0, context, [handler] {
        [NSAppleEventManager.sharedAppleEventManager setEventHandler:handler
                                                         andSelector:@selector(handleReopen:withReplyEvent:)
                                                       forEventClass:kCoreEventClass
                                                          andEventID:kAEReopenApplication];
    });
    QObject::connect(context, &QObject::destroyed, [handler] {
        [NSAppleEventManager.sharedAppleEventManager removeEventHandlerForEventClass:kCoreEventClass
                                                                          andEventID:kAEReopenApplication];
        [handler release];
    });
}

} // namespace speecher::mac
