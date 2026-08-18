#include "platform/mac/MacCorrectionObserver.h"

#import <ApplicationServices/ApplicationServices.h>

#include <utility>

namespace speecher::mac {
namespace {

QString elementValue(AXUIElementRef element)
{
    if (!element) {
        return {};
    }
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXValueAttribute, &value) != kAXErrorSuccess
        || !value) {
        return {};
    }
    QString text;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        text = QString::fromCFString(static_cast<CFStringRef>(value));
    }
    CFRelease(value);
    return text;
}

void notificationReceived(AXObserverRef, AXUIElementRef, CFStringRef notification, void *refcon)
{
    auto *observer = static_cast<CorrectionObserver *>(refcon);
    if (!observer || !notification) {
        return;
    }
    if (CFStringCompare(notification, kAXUIElementDestroyedNotification, 0) == kCFCompareEqualTo) {
        observer->elementDestroyed();
        return;
    }
    observer->valueChanged();
}

} // namespace

CorrectionObserver::CorrectionObserver()
{
    m_settle.setSingleShot(true);
    m_deadline.setSingleShot(true);
    QObject::connect(&m_settle, &QTimer::timeout, [this] { sample(); });
    QObject::connect(&m_deadline, &QTimer::timeout, [this] { cancel(); });
}

CorrectionObserver::~CorrectionObserver()
{
    stop();
}

void CorrectionObserver::setEnabled(bool enabled)
{
    m_tracker.setEnabled(enabled);
    if (!enabled) {
        stop();
    }
}

void CorrectionObserver::cancel()
{
    stop();
    m_tracker.cancel();
}

void CorrectionObserver::observe(void *element,
                                 qint64 processId,
                                 CorrectionWindow window,
                                 CorrectionTracker::Observed observed)
{
    cancel();
    // Without the Accessibility grant there are no notifications to subscribe
    // to; learning is optional, so this stays quiet rather than reporting it.
    if (!element || processId <= 0 || !AXIsProcessTrusted()) {
        return;
    }
    m_tracker.begin(std::move(window), std::move(observed));
    if (!m_tracker.active()) {
        return;
    }

    AXObserverRef observer = nullptr;
    if (AXObserverCreate(static_cast<pid_t>(processId), notificationReceived, &observer)
            != kAXErrorSuccess
        || !observer) {
        m_tracker.cancel();
        return;
    }

    AXUIElementRef target = static_cast<AXUIElementRef>(element);
    // The observation outlives the provider's own reference, which the next
    // capture releases, so it holds one of its own.
    CFRetain(target);
    m_element = element;
    m_observer = observer;

    if (AXObserverAddNotification(observer, target, kAXValueChangedNotification, this)
        != kAXErrorSuccess) {
        stop();
        m_tracker.cancel();
        return;
    }
    // Best effort: without it the observation still ends at the deadline, it
    // just cannot end early when the control goes away.
    AXObserverAddNotification(observer, target, kAXUIElementDestroyedNotification, this);
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer),
                       kCFRunLoopDefaultMode);
    m_deadline.start(correctionWindowMs);
}

void CorrectionObserver::valueChanged()
{
    sample();
    if (m_tracker.active()) {
        m_settle.start(correctionSettleMs);
    }
}

void CorrectionObserver::elementDestroyed()
{
    m_settle.stop();
    m_tracker.cancel();
}

void CorrectionObserver::sample()
{
    if (!m_tracker.active()) {
        return;
    }
    m_tracker.sample(elementValue(static_cast<AXUIElementRef>(m_element)));
}

void CorrectionObserver::stop()
{
    // Only ever called from a timer, from the provider, or from the destructor.
    // Releasing the AXObserver from inside its own callback would tear the
    // run-loop source down mid-callout, so a finished or abandoned observation
    // leaves the plumbing in place and lets the deadline timer clear it.
    m_settle.stop();
    m_deadline.stop();
    if (m_observer) {
        AXObserverRef observer = static_cast<AXObserverRef>(m_observer);
        if (m_element) {
            AXUIElementRef target = static_cast<AXUIElementRef>(m_element);
            AXObserverRemoveNotification(observer, target, kAXValueChangedNotification);
            AXObserverRemoveNotification(observer, target, kAXUIElementDestroyedNotification);
        }
        CFRunLoopRemoveSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer),
                              kCFRunLoopDefaultMode);
        // The run-loop source belongs to the observer, so releasing the observer
        // is what frees it; releasing the source too would over-release.
        CFRelease(observer);
        m_observer = nullptr;
    }
    if (m_element) {
        CFRelease(static_cast<AXUIElementRef>(m_element));
        m_element = nullptr;
    }
}

} // namespace speecher::mac
