#pragma once

// Waits on a delivery, which answers in the background, for tests that read
// its result.

#include "dictation/DictationPorts.h"

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

} // namespace speecher::test
