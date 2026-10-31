#pragma once

// Waits on a delivery or a clipboard operation, which answer in the
// background, for tests that read the answer.

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

} // namespace speecher::test
