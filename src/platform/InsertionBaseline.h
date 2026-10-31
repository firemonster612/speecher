#pragma once

#include "platform/CorrectionDiff.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace speecher {

// The focused control's text and selection just before Speecher writes into
// it. Delivery replaces exactly [start, end) with the inserted text, so the
// baseline says what the control must read afterwards. The macOS and Windows
// target providers read their controls whole, so both verify through this.
struct InsertionBaseline {
    QString before;
    int start = 0;
    int end = 0;

    // Nothing when the reported selection does not lie inside the text.
    static std::optional<InsertionBaseline> forSelection(const QString &text, int start, int end);

    // The control's text once the selection has been replaced by plainText.
    QString expectedAfter(const QString &plainText) const;

    // Reads the control until it shows exactly expectedAfter(plainText), so a
    // background edit or an ignored paste never verifies, and done hears
    // whether it did. `read` returns nothing to give up, such as when focus
    // has moved. Reads are spaced as awaitInsertion() spaces its checks.
    // Text identical to the replaced selection changes nothing and never
    // verifies.
    void verify(const QString &plainText,
                std::function<std::optional<QString>()> read,
                QObject *context,
                std::function<void(bool shown)> done) const;

    // The inserted span pinned by the text around it, or nothing when either
    // side has too little context to find the span again once edited.
    std::optional<CorrectionWindow> correctionWindow(const Target &target,
                                                     const QString &plainText) const;
};

// Asks `check` whether an insertion shows yet, a few times over about a tenth
// of a second: some controls publish a write only on their next event-loop
// turn. The first check is at once, the rest on later turns of context's
// event loop, so whatever runs between them may change the caller's state.
// `check` returns nothing to give up at once. done hears whether the
// insertion showed, unless context is gone first.
void awaitInsertion(std::function<std::optional<bool>()> check, QObject *context, std::function<void(bool shown)> done);

} // namespace speecher
