#pragma once

#include "platform/CorrectionDiff.h"

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
    // background edit or an ignored paste never verifies. `read` returns
    // nothing to give up, such as when focus has moved. Some controls publish a
    // write only on their next event-loop turn, so between reads this runs a
    // nested event loop, user input excluded. Text identical to the replaced
    // selection changes nothing and never verifies.
    bool verify(const QString &plainText, const std::function<std::optional<QString>()> &read) const;

    // The inserted span pinned by the text around it, or nothing when either
    // side has too little context to find the span again once edited.
    std::optional<CorrectionWindow> correctionWindow(const Target &target,
                                                     const QString &plainText) const;
};

} // namespace speecher
