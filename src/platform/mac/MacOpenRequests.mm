#include "platform/mac/MacOpenRequests.h"

#include <QCoreApplication>
#include <QFileOpenEvent>
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

namespace {

// Qt's application delegate turns AppKit's application:openURLs: into a
// QFileOpenEvent for the application object.
class FileOpenFilter final : public QObject {
public:
    FileOpenFilter(QObject *context, std::function<void(const QString &)> open)
        : QObject(context)
        , m_open(std::move(open))
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen) {
            return QObject::eventFilter(watched, event);
        }
        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (!path.isEmpty()) {
            m_open(path);
        }
        return true;
    }

private:
    std::function<void(const QString &)> m_open;
};

} // namespace

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

void onOpenFile(QObject *context, std::function<void(const QString &path)> open)
{
    // Parented to context, so the filter goes with it.
    QCoreApplication::instance()->installEventFilter(new FileOpenFilter(context, std::move(open)));
}

} // namespace speecher::mac
