#pragma once

// Waits on a delivery, a clipboard operation or a target provider, which can
// answer in the background, for tests that read the answer.

#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"

#include <QTest>

#include <optional>

namespace speecher::test {

// Delivers and waits for delivered(). A delivery that never answers comes
// back as a failure saying so.
inline DeliveryResult deliverAndWait(TextDeliveryAdapter &delivery,
                                     const OutputSettings &settings,
                                     const DeliveryContent &content,
                                     const Target &target,
                                     int timeoutMs = 10000)
{
    std::optional<DeliveryResult> answer;
    const QMetaObject::Connection connection = QObject::connect(
        &delivery, &TextDeliveryAdapter::delivered, [&answer](const DeliveryResult &result) { answer = result; });
    delivery.deliver(settings, content, target);
    const bool answered = QTest::qWaitFor([&answer] { return answer.has_value(); }, timeoutMs);
    QObject::disconnect(connection);
    if (!answered) {
        return {false, DeliveryReceipt::None, false,
                QStringLiteral("The delivery did not answer within %1 ms").arg(timeoutMs)};
    }
    return *answer;
}

// Runs start(context, done) for an operation that answers through done, and
// waits for the answer: a failed one saying so when none comes in time.
template <typename Answer, typename Start>
Answer waitForAnswer(Start start, int timeoutMs = 10000)
{
    QObject context;
    std::optional<Answer> answer;
    start(&context, std::function<void(const Answer &)>([&answer](const Answer &given) { answer = given; }));
    if (!QTest::qWaitFor([&answer] { return answer.has_value(); }, timeoutMs)) {
        Answer none;
        none.error = QStringLiteral("No answer within %1 ms").arg(timeoutMs);
        return none;
    }
    return *answer;
}

inline ClipboardCopied copyAndWait(ClipboardDelivery &clipboard, const DeliveryContent &content)
{
    return waitForAnswer<ClipboardCopied>([&](QObject *context, auto done) { clipboard.copy(content, context, done); });
}

inline ClipboardCaptured captureAndWait(ClipboardDelivery &clipboard)
{
    return waitForAnswer<ClipboardCaptured>([&](QObject *context, auto done) { clipboard.capture(context, done); });
}

inline ClipboardRestored restoreAndWait(ClipboardDelivery &clipboard,
                                        const ClipboardSnapshot &snapshot,
                                        bool preserveNewCopy = false)
{
    return waitForAnswer<ClipboardRestored>(
        [&](QObject *context, auto done) { clipboard.restore(snapshot, preserveNewCopy, context, done); });
}

#ifdef SPEECHER_WITH_WAYLAND
inline ClipboardCopied copyAndWait(WlClipboardDelivery &clipboard, const DeliveryContent &content)
{
    return waitForAnswer<ClipboardCopied>([&](QObject *context, auto done) { clipboard.copy(content, context, done); });
}

inline ClipboardCaptured captureWaylandAndWait()
{
    return waitForAnswer<ClipboardCaptured>(
        [](QObject *context, auto done) { WlClipboardDelivery::capture(context, done); });
}

inline ClipboardRestored restoreWaylandAndWait(const ClipboardSnapshot &snapshot)
{
    return waitForAnswer<ClipboardRestored>(
        [&](QObject *context, auto done) { WlClipboardDelivery::restore(snapshot, context, done); });
}
#endif

// Captures through provider and waits for the Target: an empty one when none
// comes in time.
inline Target captureAndWait(TargetProvider &provider, int timeoutMs = 10000)
{
    QObject context;
    std::optional<Target> captured;
    provider.capture(&context, [&captured](const Target &target) { captured = target; });
    return QTest::qWaitFor([&captured] { return captured.has_value(); }, timeoutMs) ? *captured : Target{};
}

// Asks provider how target relates to the window in front, and waits: nothing
// when no answer comes in time.
inline std::optional<TargetProvider::FocusMatch> focusMatchAndWait(TargetProvider &provider,
                                                                   const Target &target,
                                                                   int timeoutMs = 10000)
{
    QObject context;
    std::optional<TargetProvider::FocusMatch> match;
    provider.focusMatch(target, &context, [&match](TargetProvider::FocusMatch given) { match = given; });
    return QTest::qWaitFor([&match] { return match.has_value(); }, timeoutMs) ? match : std::nullopt;
}

} // namespace speecher::test
