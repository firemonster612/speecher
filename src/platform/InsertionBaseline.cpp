#include "platform/InsertionBaseline.h"

#include <QEventLoop>
#include <QTimer>

namespace speecher {
namespace {

// Most controls apply a write synchronously, but some only publish the new
// value on their next run-loop turn.
constexpr int verificationAttempts = 5;
constexpr int verificationPauseMs = 30;

// Sleeping here would freeze Speecher's own event loop, including the dictation
// popup. Spinning it instead keeps the UI alive; user input stays excluded so a
// stray keystroke cannot reach Speecher while the target still has focus.
void spinEventLoop(int milliseconds)
{
    QEventLoop wait;
    QTimer::singleShot(milliseconds, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
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

bool InsertionBaseline::verify(const QString &plainText,
                               const std::function<std::optional<QString>()> &read) const
{
    const QString expected = expectedAfter(plainText);
    if (plainText.isEmpty() || expected == before) {
        return false;
    }
    for (int attempt = 0; attempt < verificationAttempts; ++attempt) {
        if (attempt > 0) {
            spinEventLoop(verificationPauseMs);
        }
        const std::optional<QString> value = read();
        if (!value) {
            return false;
        }
        if (*value == expected) {
            return true;
        }
    }
    return false;
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
