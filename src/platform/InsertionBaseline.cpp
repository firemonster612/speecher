#include "platform/InsertionBaseline.h"

#include <QTimer>

namespace speecher {
namespace {

// Most controls apply a write synchronously, but some only publish the new
// value on their next run-loop turn.
constexpr int verificationAttempts = 5;
constexpr int verificationPauseMs = 30;

// Checks again after a pause, on the event loop's own turns: sleeping would
// freeze Speecher's UI, and a nested loop would run anything at all inside
// the caller's step.
void checkInsertion(int attempt,
                    std::function<std::optional<bool>()> check,
                    QObject *context,
                    std::function<void(bool)> done)
{
    const std::optional<bool> shown = check();
    if (!shown || *shown || attempt + 1 >= verificationAttempts) {
        done(shown.value_or(false));
        return;
    }
    QTimer::singleShot(verificationPauseMs, context, [attempt, check, context, done] {
        checkInsertion(attempt + 1, check, context, done);
    });
}

} // namespace

std::optional<InsertionBaseline> InsertionBaseline::forSelection(const QString &text,
                                                                 int start,
                                                                 int end)
{
    if (start < 0 || end < start || end > text.size()) {
        return std::nullopt;
    }
    return InsertionBaseline{text, start, end};
}

QString InsertionBaseline::expectedAfter(const QString &plainText) const
{
    return before.left(start) + plainText + before.mid(end);
}

void InsertionBaseline::verify(const QString &plainText,
                               std::function<std::optional<QString>()> read,
                               QObject *context,
                               std::function<void(bool)> done) const
{
    const QString expected = expectedAfter(plainText);
    if (plainText.isEmpty() || expected == before) {
        done(false);
        return;
    }
    awaitInsertion(
        [read = std::move(read), expected]() -> std::optional<bool> {
            const std::optional<QString> value = read();
            if (!value) {
                return std::nullopt;
            }
            return *value == expected;
        },
        context, std::move(done));
}

void awaitInsertion(std::function<std::optional<bool>()> check, QObject *context, std::function<void(bool)> done)
{
    checkInsertion(0, std::move(check), context, std::move(done));
}

std::optional<CorrectionWindow> InsertionBaseline::correctionWindow(const Target &target,
                                                                    const QString &plainText) const
{
    const QString prefix = before.left(start).right(correctionContextChars);
    const QString suffix = before.mid(end).left(correctionContextChars);
    // Learning from the wrong span is worse than not learning at all.
    if (prefix.size() < correctionMinContextChars || suffix.size() < correctionMinContextChars) {
        return std::nullopt;
    }
    return CorrectionWindow{target, plainText, prefix, suffix};
}

} // namespace speecher
